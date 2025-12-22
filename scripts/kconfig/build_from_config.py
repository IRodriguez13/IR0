#!/usr/bin/env python3
"""
IR0 Kernel Build System
Reads .config file and compiles selected subsystems using unibuild
"""

import json
import os
import sys
import subprocess
from pathlib import Path

def load_config(config_file):
    """Load configuration from .config file"""
    if not os.path.exists(config_file):
        print(f"Error: Configuration file not found: {config_file}")
        return None
    
    config = {}
    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if '=' in line:
                key, value = line.split('=', 1)
                config[key.strip()] = value.strip()
    
    return config

def load_subsystems(subsystems_json):
    """Load subsystems definition from JSON"""
    if not os.path.exists(subsystems_json):
        print(f"Error: Subsystems JSON not found: {subsystems_json}")
        return None
    
    with open(subsystems_json, 'r') as f:
        return json.load(f)

def get_selected_subsystems(config):
    """Extract selected subsystems from config"""
    selected = []
    for key, value in config.items():
        if key.startswith('SUBSYSTEM_') and value == 'y':
            subsystem = key.replace('SUBSYSTEM_', '')
            selected.append(subsystem)
    return selected

def get_subsystem_files(subsystems_data, subsystem_id, arch):
    """Get list of files for a subsystem"""
    if subsystem_id not in subsystems_data['subsystems']:
        return []
    
    subsystem = subsystems_data['subsystems'][subsystem_id]
    if arch not in subsystem.get('files', {}):
        return []
    
    return subsystem['files'][arch]

def resolve_dependencies(subsystems_data, selected_subsystems):
    """Resolve dependencies for selected subsystems"""
    resolved = set()
    to_process = set(selected_subsystems)
    
    while to_process:
        subsystem = to_process.pop()
        if subsystem in resolved:
            continue
        
        resolved.add(subsystem)
        
        # Add dependencies
        if subsystem in subsystems_data['subsystems']:
            deps = subsystems_data['subsystems'][subsystem].get('dependencies', [])
            for dep in deps:
                if dep not in resolved:
                    to_process.add(dep)
    
    return resolved

def compile_subsystem_files(kernel_root, files, arch):
    """Compile files for a subsystem using unibuild"""
    if not files:
        return True
    
    unibuild_script = os.path.join(kernel_root, 'scripts', 'unibuild.sh')
    if not os.path.exists(unibuild_script):
        print(f"Error: unibuild.sh not found at {unibuild_script}")
        return False
    
    # Build file paths relative to kernel root
    file_paths = [os.path.join(kernel_root, f) for f in files if f]
    
    if not file_paths:
        return True
    
    print(f"  Compiling {len(file_paths)} file(s)...")
    
    # Call unibuild for each file (or batch if supported)
    for file_path in file_paths:
        if not os.path.exists(file_path):
            print(f"    Warning: File not found: {file_path}")
            continue
        
        rel_path = os.path.relpath(file_path, kernel_root)
        cmd = ['bash', unibuild_script, rel_path]
        
        result = subprocess.run(cmd, cwd=kernel_root, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"    Error compiling {rel_path}:")
            print(result.stderr)
            return False
        else:
            print(f"    ✓ {rel_path}")
    
    return True

def main():
    if len(sys.argv) < 3:
        print("Usage: build_from_config.py <config_file> <subsystems_json> [arch]")
        sys.exit(1)
    
    config_file = sys.argv[1]
    subsystems_json = sys.argv[2]
    arch = sys.argv[3] if len(sys.argv) > 3 else 'x86-64'
    
    # Calculate kernel root: build_from_config.py is in scripts/kconfig/
    # Go up 2 levels: kconfig -> scripts -> root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    kernel_root = os.path.dirname(os.path.dirname(script_dir))
    
    # If config_file is relative, make it relative to kernel_root
    if not os.path.isabs(config_file):
        config_file = os.path.join(kernel_root, config_file)
    
    # If subsystems_json is relative, make it relative to script_dir
    if not os.path.isabs(subsystems_json):
        subsystems_json = os.path.join(script_dir, os.path.basename(subsystems_json))
    
    print(f"IR0 Kernel Build System")
    print(f"========================")
    print(f"Config file: {config_file}")
    print(f"Architecture: {arch}")
    print(f"Kernel root: {kernel_root}")
    print()
    
    # Load configuration
    config = load_config(config_file)
    if not config:
        sys.exit(1)
    
    # Load subsystems
    subsystems_data = load_subsystems(subsystems_json)
    if not subsystems_data:
        sys.exit(1)
    
    # Get selected subsystems
    selected = get_selected_subsystems(config)
    if not selected:
        print("Warning: No subsystems selected in configuration")
        sys.exit(1)
    
    print(f"Selected subsystems: {', '.join(selected)}")
    
    # Resolve dependencies
    all_subsystems = resolve_dependencies(subsystems_data, selected)
    print(f"Including dependencies: {', '.join(all_subsystems - set(selected))}")
    print()
    
    # Compile each subsystem
    success = True
    for subsystem_id in sorted(all_subsystems):
        print(f"Building subsystem: {subsystem_id}")
        files = get_subsystem_files(subsystems_data, subsystem_id, arch)
        
        if not files:
            print(f"  No files for {subsystem_id} on {arch}")
            continue
        
        if not compile_subsystem_files(kernel_root, files, arch):
            success = False
            break
        print()
    
    if success:
        print("✓ Build completed successfully!")
        print()
        print("Next steps:")
        print("  1. Link the kernel: make link-kernel")
        print("  2. Create ISO: make iso")
        print("  3. Run: make run")
    else:
        print("✗ Build failed!")
        sys.exit(1)

if __name__ == '__main__':
    main()

