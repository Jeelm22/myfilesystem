import os
import threading

def write_read_test(file_path, write_data):
    # Write data to the file
    with open(file_path, 'wb') as f:
        f.write(write_data)
    
    # Read data back from the file
    with open(file_path, 'rb') as f:
        read_data = f.read()
        assert read_data == write_data, "Mismatch in written and read data"

    print(f"Test passed for {len(write_data)} bytes")

def boundary_test(file_path, direct_data_size):
    # Prepare data that spans the boundary of direct data and block storage
    write_data = b'a' * (direct_data_size - 1) + b'b' * 1024  # slightly more than direct data size
    write_read_test(file_path, write_data)

    # Update data spanning the last byte of direct storage and the first byte of block storage
    with open(file_path, 'r+b') as f:
        f.seek(direct_data_size - 1)  # Seek to the last byte of direct data
        f.write(b'zz')  # Write across the boundary

    # Verify the update
    with open(file_path, 'rb') as f:
        f.seek(direct_data_size - 1)
        assert f.read(2) == b'zz', "Boundary update test failed"

    print("Boundary condition test passed")

def concurrent_write(file_path, data, thread_id):
    with open(f"{file_path}_{thread_id}", 'wb') as f:
        f.write(data)
    print(f"Thread {thread_id}: Write complete")

def stress_test(file_path, num_threads, data_size):
    threads = []
    data = os.urandom(data_size)
    for i in range(num_threads):
        thread = threading.Thread(target=concurrent_write, args=(file_path, data, i))
        threads.append(thread)
        thread.start()
    
    for thread in threads:
        thread.join()
    
    print("Stress test complete")

if __name__ == "__main__":
    DIRECT_DATA_SIZE = 128  # Assume 128 bytes for the direct data area

    # Test with different file sizes
    write_read_test("/home/dm510/dm510fs-mountpoint/smallfile", os.urandom(50))  # Smaller than direct data size
    write_read_test("/home/dm510/dm510fs-mountpoint/exactfile", os.urandom(DIRECT_DATA_SIZE))  # Exactly direct data size
    write_read_test("/home/dm510/dm510fs-mountpoint/slightlylarger", os.urandom(DIRECT_DATA_SIZE + 50))  # Slightly larger than direct data size
    write_read_test("/home/dm510/dm510fs-mountpoint/largefile", os.urandom(5000))  # Much larger file to test multiple blocks

    # Boundary condition testing
    boundary_test("/home/dm510/dm510fs-mountpoint/boundaryfile", DIRECT_DATA_SIZE)

    # Concurrency and stress testing
    stress_test("/home/dm510/dm510fs-mountpoint/concurrentfile", 10, 1024)  # 10 threads writing 1024 bytes each
