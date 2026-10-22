#!/usr/bin/env python3
"""Real TCP tests for file_server and file_client."""
import concurrent.futures
import hashlib
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest

PROJECT = Path(__file__).resolve().parents[1]
SERVER = PROJECT / 'out/host/file_server'
CLIENT = PROJECT / 'out/host/file_client'

def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def receive(sock, length):
    result = b''
    while len(result) < length:
        part = sock.recv(length - len(result))
        if not part:
            raise EOFError('Truncated response')
        result += part
    return result

class TransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SERVER.is_file() or not CLIENT.is_file():
            raise AssertionError('Host file_server and file_client have not been implemented/built')

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='file-interaction-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.share = self.base / 'shared'
        self.local = self.base / 'local'
        self.share.mkdir(); self.local.mkdir()
        (self.share / 'txt').write_bytes(b'file interaction\n')
        (self.share / 'txt').chmod(0o755)
        (self.share / 'empty').write_bytes(b'')
        (self.share / 'subdir').mkdir()
        (self.share / 'subdir/a b.bin').write_bytes(bytes(range(256)) * 8193)
        self.port = free_port()
        self.start_server(self.port)

    def start_server(self, port=None):
        args = [str(SERVER), str(self.share)]
        if port is not None:
            args.append(str(port))
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop_server, process)
        ready, _, _ = select.select([process.stdout], [], [], 5)
        self.assertTrue(ready, 'Server startup timeout')
        line = process.stdout.readline()
        self.assertIn('Serving ', line, process.stderr.read() if process.poll() is not None else line)
        return process

    @staticmethod
    def stop_server(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        process.stdout.close(); process.stderr.close()

    def client(self, remote, destination='./', port=None, success=True):
        port = self.port if port is None else port
        args = [str(CLIENT), '127.0.0.1', str(port), 'cp', remote, str(destination)]
        result = subprocess.run(args, cwd=self.local, capture_output=True, text=True, timeout=10)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def test_default_port_and_directory_destination(self):
        self.start_server()
        result = subprocess.run([str(CLIENT), '127.0.0.1', 'cp', 'SERVER-DIR/txt', './'],
                                cwd=self.local, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.local/'txt').read_bytes(), (self.share/'txt').read_bytes())
        self.assertEqual((self.local/'txt').stat().st_mode & 0o777, 0o755 & ~self.current_umask())

    @staticmethod
    def current_umask():
        mask = os.umask(0); os.umask(mask); return mask

    def test_binary_empty_and_rename(self):
        self.client('subdir/a b.bin', 'renamed.bin')
        self.client('empty')
        self.assertEqual((self.local/'empty').stat().st_size, 0)
        self.assertEqual(hashlib.sha256((self.local/'renamed.bin').read_bytes()).digest(),
                         hashlib.sha256((self.share/'subdir/a b.bin').read_bytes()).digest())

    def test_overwrite_existing_regular_file(self):
        (self.local/'txt').write_bytes(b'old')
        (self.local/'txt').chmod(0o600)
        self.client('txt')
        self.assertEqual((self.local/'txt').read_bytes(), (self.share/'txt').read_bytes())
        self.assertEqual((self.local/'txt').stat().st_mode & 0o777, 0o600)

    def test_remote_errors_do_not_change_target(self):
        (self.base/'outside').write_bytes(b'secret')
        (self.share/'escape').symlink_to(self.base/'outside')
        (self.share/'dir-link').symlink_to(self.base, target_is_directory=True)
        os.mkfifo(self.share/'pipe')
        for remote in ('missing', '../outside', '/etc/passwd', 'escape', 'dir-link/outside', 'subdir', 'pipe'):
            with self.subTest(remote=remote):
                (self.local/'target').write_bytes(b'keep')
                self.client(remote, 'target', success=False)
                self.assertEqual((self.local/'target').read_bytes(), b'keep')
        self.assertEqual(sorted(p.name for p in self.local.iterdir()), ['target'])

    def test_invalid_cli_and_destination(self):
        for args in (['127.0.0.1','0','cp','txt','.'], ['127.0.0.1','65536','cp','txt','.'],
                     ['not-an-ip','cp','txt','.'], ['127.0.0.1','rm','txt','.'],
                     ['127.0.0.1','cp','txt']):
            result = subprocess.run([str(CLIENT),*args],capture_output=True,text=True,timeout=5)
            self.assertNotEqual(result.returncode, 0)
        self.client('txt', 'missing-parent/new', success=False)
        (self.local/'target').symlink_to(self.local/'original')
        (self.local/'original').write_bytes(b'keep')
        self.client('txt','target',success=False)
        self.assertTrue((self.local/'target').is_symlink())
        self.assertEqual((self.local/'original').read_bytes(), b'keep')

    def test_concurrent_downloads_and_idle_connection(self):
        with socket.create_connection(('127.0.0.1',self.port),timeout=5):
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                futures = [pool.submit(self.client,'subdir/a b.bin',f'copy-{i}') for i in range(4)]
                for future in futures: future.result()
        for i in range(4):
            self.assertEqual((self.local/f'copy-{i}').read_bytes(),(self.share/'subdir/a b.bin').read_bytes())

    def test_malformed_requests(self):
        for request in (b'BADMAGIC'+struct.pack('!II',1,3)+b'txt',
                        b'FICP0001'+struct.pack('!II',9,3)+b'txt',
                        b'FICP0001'+struct.pack('!II',1,5000),
                        b'FICP0001'+struct.pack('!II',1,4)+b't\0xt'):
            with socket.create_connection(('127.0.0.1',self.port),timeout=5) as sock:
                sock.sendall(request)
                header = receive(sock,24)
                magic,status,size,mode = struct.unpack('!8sIQI',header)
                self.assertEqual(magic,b'FIRS0001')
                self.assertNotEqual(status,0)
                self.assertLessEqual(size,1024)
                self.assertEqual(mode,0)
                receive(sock,size)
        self.client('txt')

    def test_large_file_size_and_fragmented_request(self):
        size = 2**32 + 17
        with (self.share/'large').open('wb') as file:
            file.truncate(size)
        request = b'FICP0001' + struct.pack('!II',1,5) + b'large'
        with socket.create_connection(('127.0.0.1',self.port),timeout=5) as sock:
            for byte in request: sock.sendall(bytes([byte]))
            magic,status,received_size,mode=struct.unpack('!8sIQI',receive(sock,24))
            self.assertEqual(magic,b'FIRS0001')
            self.assertEqual(status,0)
            self.assertEqual(received_size,size)
            self.assertLessEqual(mode,0o777)
        self.client('txt')

    def test_truncated_and_invalid_responses_preserve_target(self):
        for response in (struct.pack('!8sIQI',b'FIRS0001',0,100,0o644)+b'short',
                         struct.pack('!8sIQI',b'WRONG001',0,0,0o644),
                         struct.pack('!8sIQI',b'FIRS0001',5,2**40,0)):
            with self.subTest(response=response):
                (self.local/'target').write_bytes(b'keep')
                listener = socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(1)
                errors=[]
                def respond():
                    try:
                        with listener.accept()[0] as sock:
                            sock.settimeout(5)
                            header=receive(sock,16)
                            receive(sock,struct.unpack('!8sII',header)[2])
                            sock.sendall(response)
                    except Exception as error:
                        errors.append(error)
                    finally:
                        listener.close()
                worker=threading.Thread(target=respond); worker.start()
                self.client('txt','target',port=listener.getsockname()[1],success=False)
                worker.join(timeout=6)
                self.assertFalse(worker.is_alive())
                self.assertFalse(errors,errors)
                self.assertEqual((self.local/'target').read_bytes(),b'keep')
                self.assertEqual(sorted(p.name for p in self.local.iterdir()),['target'])

    def test_maximum_length_filename(self):
        name = 'x' * 255
        (self.share/name).write_bytes(b'long filename')
        self.client(name)
        self.assertEqual((self.local/name).read_bytes(), b'long filename')

    def test_interrupt_preserves_target_and_removes_temporary_file(self):
        (self.local/'target').write_bytes(b'keep')
        listener = socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(1)
        port = listener.getsockname()[1]
        def stalled_response():
            try:
                with listener.accept()[0] as sock:
                    sock.settimeout(5)
                    header = receive(sock,16)
                    receive(sock,struct.unpack('!8sII',header)[2])
                    sock.sendall(struct.pack('!8sIQI',b'FIRS0001',0,100000,0o644))
                    try: sock.recv(1)
                    except ConnectionResetError: pass
            finally:
                listener.close()
        worker=threading.Thread(target=stalled_response); worker.start()
        process=subprocess.Popen([str(CLIENT),'127.0.0.1',str(port),'cp','txt','target'],
                                 cwd=self.local,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            deadline=time.monotonic()+5
            while len(list(self.local.iterdir())) == 1 and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(len(list(self.local.iterdir())),2)
            process.send_signal(signal.SIGTERM)
            process.communicate(timeout=5)
            self.assertNotEqual(process.returncode,0)
            self.assertEqual((self.local/'target').read_bytes(),b'keep')
            self.assertEqual(sorted(p.name for p in self.local.iterdir()),['target'])
        finally:
            if process.poll() is None:
                process.kill(); process.communicate()
            worker.join(timeout=6)
            self.assertFalse(worker.is_alive())

    def test_signal_at_commit_reports_completed_copy(self):
        library=self.base/'interrupt-rename.so'
        subprocess.run(['gcc','-shared','-fPIC','-Wall','-Wextra','-Werror',
                        str(PROJECT/'tests/interrupt_rename.c'),'-ldl','-o',str(library)],check=True)
        (self.local/'target').write_bytes(b'keep')
        result=subprocess.run([str(CLIENT),'127.0.0.1',str(self.port),'cp','txt','target'],
                              cwd=self.local,capture_output=True,text=True,timeout=10,
                              env=dict(os.environ,LD_PRELOAD=str(library)))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual((self.local/'target').read_bytes(),(self.share/'txt').read_bytes())
        self.assertEqual(sorted(p.name for p in self.local.iterdir()),['target'])

if __name__ == '__main__':
    unittest.main(verbosity=2)
