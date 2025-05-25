import os

class ZigbeeStorage:
    def __init__(self, storage_path="devices"):
        self.storage_path = storage_path

        try:
            os.mkdir(storage_path)
        except OSError:  # MicroPython
            pass
            
    def file_exists(self, path):
        try:
            os.stat(path)
            return True
        except OSError:
            return False

    def storage_handler(self, cmd, *args):
        """Callback for saving/loading devices"""

        print("Storage command:", cmd, args)

        if cmd == "save":
            filename, data = args
            # В MicroPython use simple string concatenation
            filepath = self.storage_path + "/" + filename
            try:
                # В MicroPython not os.makedirs  exist_ok
                try:
                    os.mkdir(self.storage_path)
                except OSError:
                    pass
                with open(filepath, 'w') as f:
                    f.write(data)
                return True
            except OSError as e:
                print("Failed to save device", filename, ":", e)
                return None
                
        elif cmd == "load":
            filename = args[0]
            # Use simple string concatenation
            filepath = self.storage_path + "/" + filename
            # First, check if the file exists
            if not self.file_exists(filepath):
                return None
            # File exists, read it
            try:
                with open(filepath) as f:
                    return f.read()
            except OSError:
                return None
                
        elif cmd == "list":
            # First, check if the directory exists
            if not self.file_exists(self.storage_path):
                return []
            try:
                # In MicroPython os.listdir returns only file names
                files = []
                for f in os.listdir(self.storage_path):
                    # Check file extension
                    if len(f) > 5 and f[-5:] == '.json':
                        # Check if file really exists
                        if self.file_exists(self.storage_path + "/" + f):
                            files.append(f)
                return files
            except OSError:
                return []
        return None
