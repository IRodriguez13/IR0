#!/usr/bin/env python3
"""
IR0 Kernel Configuration Build Wrapper
Python wrapper for kconfig_build C library
"""

import ctypes
import os
import sys
from pathlib import Path

class KConfigBuild:
    """Wrapper for kconfig_build C library"""
    
    def __init__(self):
        """Initialize and load the C library"""
        script_dir = os.path.dirname(os.path.abspath(__file__))
        kernel_root = os.path.dirname(os.path.dirname(script_dir))
        
        # Try to load shared library
        lib_paths = [
            os.path.join(script_dir, 'libkconfig_build.so'),
            os.path.join(kernel_root, 'setup', 'libkconfig_build.so'),
        ]
        
        self.lib = None
        for lib_path in lib_paths:
            if os.path.exists(lib_path):
                try:
                    self.lib = ctypes.CDLL(lib_path)
                    self._setup_functions()
                    break
                except Exception as e:
                    print(f"Warning: Could not load library from {lib_path}: {e}")
                    continue
        
        if not self.lib:
            # Fallback to subprocess if library not available
            self.lib = None
            print("Warning: C library not found, using subprocess fallback")
    
    def _setup_functions(self):
        """Setup C function signatures"""
        if not self.lib:
            return
        
        # kconfig_build_file
        self.lib.kconfig_build_file.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        self.lib.kconfig_build_file.restype = ctypes.c_int
        
        # kconfig_build_from_config
        self.lib.kconfig_build_from_config.argtypes = [
            ctypes.c_char_p,  # config_file
            ctypes.c_char_p,  # subsystems_json
            ctypes.c_char_p,  # arch
            ctypes.c_char_p   # kernel_root
        ]
        self.lib.kconfig_build_from_config.restype = ctypes.c_int
        
        # kconfig_get_kernel_root
        self.lib.kconfig_get_kernel_root.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        self.lib.kconfig_get_kernel_root.restype = ctypes.c_int
        
        # kconfig_generate_makefile
        self.lib.kconfig_generate_makefile.argtypes = [
            ctypes.c_char_p,  # config_file
            ctypes.c_char_p,  # subsystems_json
            ctypes.c_char_p,  # arch
            ctypes.c_char_p   # kernel_root
        ]
        self.lib.kconfig_generate_makefile.restype = ctypes.c_int
        
        # kconfig_build_dynamic_makefile
        self.lib.kconfig_build_dynamic_makefile.argtypes = [ctypes.c_char_p]
        self.lib.kconfig_build_dynamic_makefile.restype = ctypes.c_int
        
        # kconfig_clean_dynamic_makefile
        self.lib.kconfig_clean_dynamic_makefile.argtypes = [ctypes.c_char_p]
        self.lib.kconfig_clean_dynamic_makefile.restype = ctypes.c_int
    
    def get_kernel_root(self):
        """Get kernel root directory"""
        if self.lib:
            buffer = ctypes.create_string_buffer(4096)
            if self.lib.kconfig_get_kernel_root(buffer, 4096) == 0:
                return buffer.value.decode('utf-8')
        
        # Fallback: search from current directory
        current = os.path.abspath('.')
        while current != '/':
            if os.path.exists(os.path.join(current, 'Makefile')) and \
               os.path.exists(os.path.join(current, 'scripts', 'unibuild.sh')):
                return current
            current = os.path.dirname(current)
        
        return None
    
    def build_file(self, file_path, kernel_root=None):
        """Build a single file using unibuild"""
        if not kernel_root:
            kernel_root = self.get_kernel_root()
        
        if not kernel_root:
            return False, "Could not find kernel root"
        
        if self.lib:
            # Use C library
            result = self.lib.kconfig_build_file(
                file_path.encode('utf-8'),
                kernel_root.encode('utf-8')
            )
            return result == 0, f"Exit code: {result}"
        else:
            # Fallback to subprocess
            import subprocess
            unibuild_script = os.path.join(kernel_root, 'scripts', 'unibuild.sh')
            if not os.path.exists(unibuild_script):
                return False, f"unibuild.sh not found at {unibuild_script}"
            
            try:
                result = subprocess.run(
                    ['bash', unibuild_script, file_path],
                    cwd=kernel_root,
                    capture_output=True,
                    text=True
                )
                return result.returncode == 0, result.stderr if result.returncode != 0 else "Success"
            except Exception as e:
                return False, str(e)
    
    def build_from_config(self, config_file, subsystems_json, arch='x86-64', kernel_root=None):
        """Build subsystems from configuration file"""
        if not kernel_root:
            kernel_root = self.get_kernel_root()
        
        if not kernel_root:
            return False, "Could not find kernel root"
        
        if self.lib:
            # Use C library
            result = self.lib.kconfig_build_from_config(
                config_file.encode('utf-8'),
                subsystems_json.encode('utf-8'),
                arch.encode('utf-8'),
                kernel_root.encode('utf-8')
            )
            return result == 0, f"Exit code: {result}"
        else:
            # Fallback to subprocess
            import subprocess
            build_script = os.path.join(kernel_root, 'scripts', 'kconfig', 'build_from_config.py')
            if not os.path.exists(build_script):
                return False, f"build_from_config.py not found at {build_script}"
            
            try:
                result = subprocess.run(
                    ['python3', build_script, config_file, subsystems_json, arch],
                    cwd=kernel_root,
                    capture_output=True,
                    text=True
                )
                return result.returncode == 0, result.stderr if result.returncode != 0 else result.stdout
            except Exception as e:
                return False, str(e)
    
    def generate_makefile(self, config_file, subsystems_json, arch='x86-64', kernel_root=None):
        """Generate dynamic Makefile based on selected subsystems"""
        if not kernel_root:
            kernel_root = self.get_kernel_root()
        
        if not kernel_root:
            return False, "Could not find kernel root"
        
        # Always use Python script for reliability (better JSON parsing)
        import subprocess
        script_dir = os.path.dirname(os.path.abspath(__file__))
        generate_script = os.path.join(script_dir, 'generate_dynamic_makefile.py')
        
        if not os.path.exists(generate_script):
            return False, f"generate_dynamic_makefile.py not found at {generate_script}"
        
        try:
            result = subprocess.run(
                ['python3', generate_script, config_file, subsystems_json, arch, kernel_root],
                capture_output=True,
                text=True,
                cwd=kernel_root
            )
            if result.returncode == 0:
                return True, result.stderr if result.stderr else "Makefile generated successfully"
            else:
                return False, result.stderr if result.stderr else f"Exit code: {result.returncode}"
        except Exception as e:
            return False, str(e)
    
    def build_dynamic(self, kernel_root=None):
        """Build using dynamic Makefile"""
        if not kernel_root:
            kernel_root = self.get_kernel_root()
        
        if not kernel_root:
            return False, "Could not find kernel root"
        
        if self.lib:
            # Use C library
            result = self.lib.kconfig_build_dynamic_makefile(
                kernel_root.encode('utf-8')
            )
            return result == 0, f"Exit code: {result}"
        else:
            # Fallback to subprocess
            import subprocess
            makefile_path = os.path.join(kernel_root, 'setup', '.build', 'Makefile.dynamic')
            if not os.path.exists(makefile_path):
                return False, "Dynamic Makefile not found. Run configuration first."
            
            try:
                result = subprocess.run(
                    ['make', '-f', makefile_path, '-C', kernel_root, 'all'],
                    capture_output=True,
                    text=True
                )
                return result.returncode == 0, result.stderr if result.returncode != 0 else result.stdout
            except Exception as e:
                return False, str(e)
    
    def clean_dynamic(self, kernel_root=None):
        """Clean using dynamic Makefile"""
        if not kernel_root:
            kernel_root = self.get_kernel_root()
        
        if not kernel_root:
            return False, "Could not find kernel root"
        
        if self.lib:
            # Use C library
            result = self.lib.kconfig_clean_dynamic_makefile(
                kernel_root.encode('utf-8')
            )
            return result == 0, f"Exit code: {result}"
        else:
            # Fallback to subprocess
            import subprocess
            makefile_path = os.path.join(kernel_root, 'setup', '.build', 'Makefile.dynamic')
            if not os.path.exists(makefile_path):
                return True, "No Makefile to clean"  # Not an error
            
            try:
                result = subprocess.run(
                    ['make', '-f', makefile_path, '-C', kernel_root, 'clean'],
                    capture_output=True,
                    text=True
                )
                return result.returncode == 0, result.stderr if result.returncode != 0 else result.stdout
            except Exception as e:
                return False, str(e)


# Convenience functions for direct use
def build_file(file_path, kernel_root=None):
    """Build a single file"""
    kconfig = KConfigBuild()
    return kconfig.build_file(file_path, kernel_root)

def build_from_config(config_file, subsystems_json, arch='x86-64', kernel_root=None):
    """Build from configuration"""
    kconfig = KConfigBuild()
    return kconfig.build_from_config(config_file, subsystems_json, arch, kernel_root)


if __name__ == '__main__':
    # Test/CLI interface
    if len(sys.argv) < 2:
        print("Usage: kconfig_build_wrapper.py <command> [args...]")
        print("Commands:")
        print("  build-file <file> [kernel_root]")
        print("  build-config <config> <subsystems_json> <arch> [kernel_root]")
        sys.exit(1)
    
    kconfig = KConfigBuild()
    
    if sys.argv[1] == 'build-file':
        if len(sys.argv) < 3:
            print("Usage: build-file <file> [kernel_root]")
            sys.exit(1)
        success, msg = kconfig.build_file(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
        print(msg)
        sys.exit(0 if success else 1)
    
    elif sys.argv[1] == 'build-config':
        if len(sys.argv) < 5:
            print("Usage: build-config <config> <subsystems_json> <arch> [kernel_root]")
            sys.exit(1)
        success, msg = kconfig.build_from_config(
            sys.argv[2], sys.argv[3], sys.argv[4],
            sys.argv[5] if len(sys.argv) > 5 else None
        )
        print(msg)
        sys.exit(0 if success else 1)
    
    else:
        print(f"Unknown command: {sys.argv[1]}")
        sys.exit(1)

