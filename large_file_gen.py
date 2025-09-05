import os
import random
import string

def generate_large_file(filename, target_size_mb=1000, batch_size=10000):
    """
    Generate a file with fake SQL rows until it reaches approximately target_size_mb MB.
    Each row is in the format: 'id,random_16_char_string,random_integer\n'
    Uses batch writing to improve performance.
    """
    target_size_bytes = target_size_mb * 1024 * 1024  # Convert MB to bytes
    with open(filename, 'w', buffering=1024*1024) as f:  # Use 1MB buffer
        id = 1
        batch = []
        while os.path.getsize(filename) < target_size_bytes:
            # Generate 16-character random string
            char_field = ''.join(random.choices(string.ascii_lowercase, k=16))
            # Generate random integer (0 to 9999999)
            int_field = random.randint(0, 9999999)
            # Create row
            row = f"{id},{char_field},{int_field}\n"
            batch.append(row)
            id += 1
            if len(batch) >= batch_size:
                f.write(''.join(batch))
                batch = []
        if batch:  # Write any remaining rows
            f.write(''.join(batch))

# Generate a 1GB file
generate_large_file("fake_sql_data.txt", target_size_mb=1000)
