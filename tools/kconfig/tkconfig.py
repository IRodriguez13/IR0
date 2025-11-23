#!/usr/bin/env python3

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import os
import json
import subprocess
import sys
import datetime
import threading
import platform

# ===============================================================================
# PLATFORM DETECTION AND COMMAND ADAPTATION
# ===============================================================================

class PlatformCommands:
    """Encapsulates platform-specific commands"""
    
    COMMANDS = {
        "windows": {
            "make": "mingw32-make",
            "python": "python",
            "qemu": "qemu-system-x86_64w.exe",
            "shell": "bash"
        },
        "linux": {
            "make": "make",
            "python": "python3",
            "qemu": "qemu-system-x86_64",
            "shell": "sh"
        },
        "macos": {
            "make": "make",
            "python": "python3",
            "qemu": "qemu-system-x86_64",
            "shell": "sh"
        }
    }
    
    def __init__(self, platform_name=None):
        if platform_name is None:
            platform_name = self.detect_platform()
        self.platform = platform_name
        self.commands = self.COMMANDS.get(platform_name, self.COMMANDS["linux"])
    
    @staticmethod
    def detect_platform():
        """Auto-detect current platform"""
        system = platform.system()
        if system == "Windows":
            return "windows"
        elif system == "Darwin":
            return "macos"
        else:
            return "linux"
    
    def get_make_cmd(self):
        return self.commands["make"]
    
    def get_python_cmd(self):
        return self.commands["python"]
    
    def get_qemu_cmd(self):
        return self.commands["qemu"]
    
    def get_shell_cmd(self):
        return self.commands["shell"]


class PlatformSelectorDialog:
    """Dialog for selecting build platform"""
    
    def __init__(self, parent):
        self.result = None
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Select Build Platform")
        self.dialog.geometry("500x320")
        self.dialog.resizable(False, False)
        
        # Center dialog
        self.dialog.transient(parent)
        self.dialog.grab_set()
        
        # Detect current platform
        detected = PlatformCommands.detect_platform()
        
        # Header
        header_frame = ttk.Frame(self.dialog, padding=20)
        header_frame.pack(fill='x')
        
        ttk.Label(header_frame, text="Select Your Build Platform", 
                 font=('Helvetica', 14, 'bold')).pack()
        ttk.Label(header_frame, text="This determines which build tools will be used",
                 font=('Helvetica', 9), foreground='gray').pack(pady=(5, 0))
        
        # Platform selection
        selection_frame = ttk.LabelFrame(self.dialog, text="Platform", padding=20)
        selection_frame.pack(fill='both', expand=True, padx=20, pady=10)
        
        self.platform_var = tk.StringVar(value=detected)
        
        platforms = [
            ("windows", "Windows", "mingw32-make, python, qemu-system-x86_64w.exe"),
            ("linux", "Linux", "make, python3, qemu-system-x86_64"),
            ("macos", "macOS", "make, python3, qemu-system-x86_64")
        ]
        
        for platform_id, platform_name, description in platforms:
            frame = ttk.Frame(selection_frame)
            frame.pack(fill='x', pady=5)
            
            radio = ttk.Radiobutton(frame, text=platform_name, 
                                   variable=self.platform_var, value=platform_id)
            radio.pack(side='left', anchor='w')
            
            if platform_id == detected:
                ttk.Label(frame, text="(Detected)", foreground='green',
                         font=('Helvetica', 8, 'italic')).pack(side='left', padx=(5, 0))
            
            ttk.Label(frame, text=description, foreground='gray',
                     font=('Helvetica', 8)).pack(anchor='w', padx=(25, 0))
        
        # Buttons
        button_frame = ttk.Frame(self.dialog, padding=10)
        button_frame.pack(fill='x', side='bottom')
        
        ttk.Button(button_frame, text="OK", command=self.on_ok).pack(side='right', padx=5)
        ttk.Button(button_frame, text="Cancel", command=self.on_cancel).pack(side='right')
        
        # Wait for dialog to close
        self.dialog.wait_window()
    
    def on_ok(self):
        self.result = self.platform_var.get()
        self.dialog.destroy()
    
    def on_cancel(self):
        self.result = None
        self.dialog.destroy()

class KernelArchitectureCanvas:
    """Canvas widget for displaying kernel architecture diagram"""
    
    def __init__(self, parent, subsystems_data, architecture_data, on_subsystem_click=None):
        self.parent = parent
        self.subsystems_data = subsystems_data
        self.architecture_data = architecture_data
        self.on_subsystem_click = on_subsystem_click
        self.selected_subsystems = set()
        self.hovered_subsystem = None
        self.highlighted_subsystem = None
        
        # Create canvas with scrollbar
        self.canvas_frame = ttk.Frame(parent)
        self.canvas_frame.pack(expand=True, fill='both', padx=5, pady=5)
        
        self.canvas = tk.Canvas(self.canvas_frame, bg='white', highlightthickness=1, highlightbackground='gray')
        scrollbar = ttk.Scrollbar(self.canvas_frame, orient='vertical', command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=scrollbar.set)
        
        scrollbar.pack(side='right', fill='y')
        self.canvas.pack(side='left', expand=True, fill='both')
        
        # Bind events
        self.canvas.bind('<Button-1>', self.on_click)
        self.canvas.bind('<Motion>', self.on_motion)
        self.canvas.bind('<Leave>', self.on_leave)
        
        # Store subsystem rectangles
        self.subsystem_rects = {}
        self.subsystem_positions = {}
        
        self.draw_architecture()
    
    def draw_architecture(self):
        """Draw the kernel architecture diagram"""
        self.canvas.delete('all')
        self.subsystem_rects = {}
        self.subsystem_positions = {}
        
        # Calculate dimensions (larger for better visibility)
        canvas_width = self.canvas.winfo_width() if self.canvas.winfo_width() > 1 else 1000
        
        # Calculate dynamic canvas height based on subsystem count in largest layer
        max_subsystems = max(len(layer['subsystems']) for layer in self.architecture_data['layers'])
        # Adjust height for layers with many subsystems
        base_layer_height = 120
        if max_subsystems > 6:
            base_layer_height = 140
        
        canvas_height = max(800, len(self.architecture_data['layers']) * (base_layer_height + 25))
        
        # Set canvas scroll region
        self.canvas.config(scrollregion=(0, 0, canvas_width, canvas_height))
        
        # Draw layers
        layer_width = canvas_width - 40
        layer_spacing = 25
        start_x = 20
        start_y = 20
        
        for layer_idx, layer in enumerate(self.architecture_data['layers']):
            # Use dynamic height for layers with many subsystems
            num_subsystems = len(layer['subsystems'])
            layer_height = base_layer_height if num_subsystems <= 6 else 140
            
            y_pos = start_y + layer_idx * (layer_height + layer_spacing)
            
            # Draw layer background
            self.canvas.create_rectangle(
                start_x, y_pos, start_x + layer_width, y_pos + layer_height,
                fill='#f0f0f0', outline='#888888', width=2, tags='layer'
            )
            
            # Draw layer name
            self.canvas.create_text(
                start_x + 10, y_pos + 15,
                text=layer['name'], anchor='w',
                font=('Arial', 12, 'bold'), fill='#333333', tags='layer'
            )
            
            # Draw subsystems in this layer
            subsystems_in_layer = layer['subsystems']
            num_subsystems = len(subsystems_in_layer)
            if num_subsystems > 0:
                # Dynamic subsystem width calculation
                # For layers with many subsystems, reduce size to fit better
                available_width = layer_width - 30
                min_subsystem_width = 90  # Minimum width for readability
                subsystem_spacing = 8 if num_subsystems > 6 else 10
                
                # Calculate subsystem width ensuring minimum size
                calculated_width = (available_width - (num_subsystems - 1) * subsystem_spacing) / num_subsystems
                subsystem_width = max(min_subsystem_width, calculated_width)
                
                # If subsystems are too wide, use scrollable approach or wrap to multiple rows
                if subsystem_width * num_subsystems + (num_subsystems - 1) * subsystem_spacing > available_width:
                    # Use minimum width and allow horizontal scrolling conceptually
                    subsystem_width = min_subsystem_width
                
                for idx, subsystem_id in enumerate(subsystems_in_layer):
                    if subsystem_id not in self.subsystems_data:
                        continue
                    
                    subsystem = self.subsystems_data[subsystem_id]
                    x_pos = start_x + 15 + idx * (subsystem_width + subsystem_spacing)
                    
                    # Determine color based on state
                    if subsystem_id == self.highlighted_subsystem:
                        fill_color = '#2196F3'  # Blue when highlighted/selected
                        outline_color = '#1976D2'
                        outline_width = 3
                    elif subsystem_id in self.selected_subsystems:
                        fill_color = '#4CAF50'  # Green when enabled
                        outline_color = '#2E7D32'
                        outline_width = 2
                    elif subsystem_id == self.hovered_subsystem:
                        fill_color = '#81C784'  # Light green on hover
                        outline_color = '#388E3C'
                        outline_width = 2
                    elif subsystem.get('required', False):
                        fill_color = '#FFC107'  # Amber for required
                        outline_color = '#F57C00'
                        outline_width = 2
                    else:
                        fill_color = '#E0E0E0'  # Gray for unselected
                        outline_color = '#9E9E9E'
                        outline_width = 2
                    
                    # Draw subsystem box (adjust height for better fit)
                    box_height = layer_height - 45
                    rect_id = self.canvas.create_rectangle(
                        x_pos, y_pos + 35, x_pos + subsystem_width - subsystem_spacing, y_pos + 35 + box_height,
                        fill=fill_color, outline=outline_color, width=outline_width,
                        tags=('subsystem', subsystem_id)
                    )
                    
                    self.subsystem_rects[subsystem_id] = rect_id
                    self.subsystem_positions[subsystem_id] = (x_pos, y_pos + 35, 
                                                               x_pos + subsystem_width - subsystem_spacing, 
                                                               y_pos + 35 + box_height)
                    
                    # Draw subsystem name (adjust font size for smaller boxes)
                    font_size = 9 if num_subsystems > 6 else 10
                    text_y = y_pos + 35 + box_height // 2
                    
                    # Truncate long names if needed
                    display_name = subsystem['name']
                    if len(display_name) > 12 and num_subsystems > 6:
                        display_name = display_name[:10] + "..."
                    
                    self.canvas.create_text(
                        x_pos + (subsystem_width - subsystem_spacing) // 2, text_y,
                        text=display_name, anchor='center',
                        font=('Arial', font_size, 'bold'), fill='#000000', tags=('subsystem', subsystem_id)
                    )
                    
                    # Draw required indicator (smaller for dense layers)
                    if subsystem.get('required', False):
                        req_font_size = 7 if num_subsystems > 6 else 8
                        self.canvas.create_text(
                            x_pos + (subsystem_width - subsystem_spacing) // 2, text_y + 15,
                            text='[Required]', anchor='center',
                            font=('Arial', req_font_size), fill='#666666', tags=('subsystem', subsystem_id)
                        )
        
        # Draw connections between layers (simplified)
        self.draw_connections()
    
    def draw_connections(self):
        """Draw connections between subsystems"""
        # Simple vertical lines connecting layers
        canvas_width = self.canvas.winfo_width() if self.canvas.winfo_width() > 1 else 600
        center_x = canvas_width // 2
        
        layers = self.architecture_data['layers']
        for i in range(len(layers) - 1):
            if i < len(layers) - 1:
                y1 = 20 + (i + 1) * 120
                y2 = 20 + (i + 1) * 120 + 20
                self.canvas.create_line(
                    center_x, y1, center_x, y2,
                    fill='#CCCCCC', width=1, dash=(5, 5), tags='connection'
                )
    
    def on_click(self, event):
        """Handle click on subsystem"""
        item = self.canvas.find_closest(event.x, event.y)[0]
        tags = self.canvas.gettags(item)
        
        if 'subsystem' in tags:
            subsystem_id = None
            for tag in tags:
                if tag != 'subsystem' and tag in self.subsystems_data:
                    subsystem_id = tag
                    break
            
            if subsystem_id:
                if self.on_subsystem_click:
                    self.on_subsystem_click(subsystem_id)
    
    def on_motion(self, event):
        """Handle mouse motion for hover effect"""
        item = self.canvas.find_closest(event.x, event.y)[0]
        tags = self.canvas.gettags(item)
        
        hovered = None
        if 'subsystem' in tags:
            for tag in tags:
                if tag != 'subsystem' and tag in self.subsystems_data:
                    hovered = tag
                    break
        
        if hovered != self.hovered_subsystem:
            self.hovered_subsystem = hovered
            self.draw_architecture()
    
    def on_leave(self, event):
        """Handle mouse leave"""
        if self.hovered_subsystem:
            self.hovered_subsystem = None
            self.draw_architecture()
    
    def set_selected_subsystems(self, selected):
        """Update selected subsystems"""
        self.selected_subsystems = set(selected)
        self.draw_architecture()
    
    def highlight_subsystem(self, subsystem_id):
        """Highlight a specific subsystem in the diagram"""
        self.highlighted_subsystem = subsystem_id
        self.draw_architecture()
        # Clear highlight after 2 seconds
        self.canvas.after(2000, lambda: self.clear_highlight())
    
    def clear_highlight(self):
        """Clear subsystem highlight"""
        self.highlighted_subsystem = None
        self.draw_architecture()
    
    def update_size(self):
        """Update canvas size"""
        self.draw_architecture()


class KernelConfigGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("IR0 Kernel Configuration")
        self.root.geometry("1800x1000")
        self.root.minsize(1600, 900)
        
        # Load kernel version
        self.kernel_version = self.get_kernel_version()
        
        # Load subsystems configuration
        self.subsystems_data = {}
        self.architecture_data = {}
        self.profiles_data = {}
        self.current_architecture = 'x86-64'  # Default architecture
        self.load_subsystems_config()
        
        # Configuration data
        self.config = {}
        self.load_config()
        
        # Initialize platform commands
        self.init_platform()
        
        # Create GUI
        self.setup_ui()
        
        # Load kernel configuration options
        self.load_kernel_options()
    
    def get_kernel_version(self):
        try:
            with open('Makefile', 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('IR0_VERSION_STRING :='):
                        version = "0.0.1 pre-release 1"
                        return f"v{version}"
        except Exception as e:
            print(f"Error reading kernel version: {e}")
        return "v0.0.1-pre-rc1"
    
    def load_subsystems_config(self):
        """Load subsystems configuration from JSON"""
        script_dir = os.path.dirname(os.path.abspath(__file__))
        config_path = os.path.join(script_dir, 'subsystems.json')
        
        if os.path.exists(config_path):
            try:
                with open(config_path, 'r') as f:
                    data = json.load(f)
                    self.subsystems_data = data.get('subsystems', {})
                    self.architecture_data = data.get('architecture', {})
                    self.profiles_data = data.get('profiles', {})
            except Exception as e:
                print(f"Error loading subsystems config: {e}")
                self.subsystems_data = {}
                self.architecture_data = {'layers': []}
        else:
            print(f"Warning: subsystems.json not found at {config_path}")
            self.subsystems_data = {}
            self.architecture_data = {'layers': []}
    
    def load_config(self):
        """Load configuration from .config file"""
        if os.path.exists('.config'):
            try:
                with open('.config', 'r') as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith('#'):
                            if '=' in line:
                                key, value = line.split('=', 1)
                                self.config[key] = value.strip('\n\r\t ')
            except Exception as e:
                print(f"Error loading config: {e}")
        
        # Initialize subsystem states from config
        for subsystem_id in self.subsystems_data:
            config_key = f"SUBSYSTEM_{subsystem_id}"
            if config_key not in self.config:
                # Default: required subsystems are enabled, others are disabled
                if self.subsystems_data[subsystem_id].get('required', False):
                    self.config[config_key] = 'y'
                else:
                    self.config[config_key] = 'n'
    
    def init_platform(self):
        """Initialize platform commands"""
        # Check if platform is already configured
        if 'BUILD_PLATFORM' not in self.config:
            # Show platform selector dialog
            dialog = PlatformSelectorDialog(self.root)
            if dialog.result:
                self.config['BUILD_PLATFORM'] = dialog.result
            else:
                # User cancelled, use auto-detected platform
                self.config['BUILD_PLATFORM'] = PlatformCommands.detect_platform()
        
        # Initialize platform commands
        self.platform_commands = PlatformCommands(self.config.get('BUILD_PLATFORM'))
        
        # Update window title with platform
        platform_name = self.config.get('BUILD_PLATFORM', 'unknown').capitalize()
        self.root.title(f"IR0 Kernel Configuration ({platform_name})")
    
    def save_config(self):
        """Save configuration to .config file"""
        try:
            with open('.config', 'w') as f:
                f.write("# IR0 Kernel Configuration\n")
                f.write("# Generated by IR0 Kernel Config Tool\n")
                f.write(f"# {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
                
                sorted_keys = sorted(self.config.keys())
                
                for key in sorted_keys:
                    if self.config[key] == 'y':
                        f.write(f"{key}=y\n")
                
                for key in sorted_keys:
                    if self.config[key] not in ['y', 'n']:
                        f.write(f"{key}={self.config[key]}\n")
                
                for key in sorted_keys:
                    if self.config[key] == 'n':
                        f.write(f"# {key} is not set\n")
            
            # Create autoconf.h
            os.makedirs('include/generated', exist_ok=True)
            with open('include/generated/autoconf.h', 'w') as f:
                f.write("/* AUTOGENERATED BY KCONFIG - DO NOT EDIT */\n")
                f.write("#ifndef __AUTOCONF_H__\n")
                f.write("#define __AUTOCONF_H__\n\n")
                
                for key, value in self.config.items():
                    if value == 'y':
                        f.write(f"#define {key} 1\n")
                    elif value == 'n':
                        f.write(f"#undef {key}\n")
                    else:
                        if '=' in key:
                            k, v = key.split('=', 1)
                            f.write(f"#define {k} {v}\n")
                        else:
                            f.write(f"#define {key} \"{value}\"\n")
                
                f.write("\n#endif /* __AUTOCONF_H__ */\n")
            
            messagebox.showinfo("Success", "Configuration saved to .config and include/generated/autoconf.h")
            return True
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save configuration: {e}")
            return False
    
    def on_subsystem_toggle(self, subsystem_id):
        """Handle subsystem checkbox toggle"""
        config_key = f"SUBSYSTEM_{subsystem_id}"
        subsystem = self.subsystems_data.get(subsystem_id, {})
        
        # Get current checkbox state (after toggle - tkinter already toggled it)
        if subsystem_id in self.subsystem_vars:
            new_checkbox_state = self.subsystem_vars[subsystem_id].get()
        else:
            new_checkbox_state = not (self.config.get(config_key, 'n') == 'y')
        
        # If required and trying to disable, prevent it
        if subsystem.get('required', False) and not new_checkbox_state:
            # Reset checkbox to checked
            if subsystem_id in self.subsystem_vars:
                self.subsystem_vars[subsystem_id].set(True)
            messagebox.showwarning("Required Subsystem", 
                                 f"{subsystem.get('name', subsystem_id)} is a required subsystem and cannot be disabled.\n\n"
                                 f"Required subsystems are essential for kernel operation and must remain enabled.")
            return
        
        # Update the config value based on checkbox state
        new_value = 'y' if new_checkbox_state else 'n'
        self.config[config_key] = new_value
        
        # Update architecture diagram immediately
        selected = [sid for sid in self.subsystems_data.keys() 
                   if self.config.get(f"SUBSYSTEM_{sid}", 'n') == 'y']
        self.arch_canvas.set_selected_subsystems(selected)
        
        # Update details and highlight in diagram
        self.current_subsystem = subsystem_id
        self.update_subsystem_details(subsystem_id)
        
        # Highlight the subsystem in the diagram (briefly)
        self.arch_canvas.highlight_subsystem(subsystem_id)
    
    def on_subsystem_click_arch(self, subsystem_id):
        """Handle click on subsystem in architecture diagram"""
        self.on_subsystem_toggle(subsystem_id)
    
    def build_subsystem(self, subsystem_id):
        """Build a specific subsystem using unibuild"""
        subsystem = self.subsystems_data.get(subsystem_id, {})
        # Get files for current architecture (default to x86-64)
        arch = self.current_architecture
        files_dict = subsystem.get('files', {})
        files = files_dict.get(arch, files_dict.get('x86-64', []))
        
        if not files:
            messagebox.showwarning("No Files", f"No source files defined for {subsystem.get('name', subsystem_id)}")
            return
        
        # Check if subsystem is enabled
        config_key = f"SUBSYSTEM_{subsystem_id}"
        if self.config.get(config_key, 'n') != 'y':
            messagebox.showwarning("Subsystem Disabled", 
                                 f"{subsystem.get('name', subsystem_id)} is not enabled. Enable it first.")
            return
        
        # Build using unibuild
        self.status_var.set(f"Building {subsystem.get('name', subsystem_id)}...")
        
        def build_thread():
            try:
                script_dir = os.path.dirname(os.path.abspath(__file__))
                kernel_root = os.path.dirname(os.path.dirname(script_dir))
                unibuild_script = os.path.join(kernel_root, 'scripts', 'unibuild.sh')
                
                # Build all files in the subsystem using platform-specific shell
                shell_cmd = self.platform_commands.get_shell_cmd()
                cmd = [shell_cmd, unibuild_script] + files
                process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    universal_newlines=True,
                    cwd=kernel_root
                )
                
                output_lines = []
                for line in process.stdout:
                    output_lines.append(line)
                    print(line.strip())
                
                process.wait()
                
                if process.returncode == 0:
                    self.root.after(0, lambda: self.status_var.set(
                        f"✓ {subsystem.get('name', subsystem_id)} built successfully"))
                    self.root.after(0, lambda: messagebox.showinfo("Build Success", 
                        f"{subsystem.get('name', subsystem_id)} built successfully!"))
                else:
                    error_msg = '\n'.join(output_lines[-10:])  # Last 10 lines
                    self.root.after(0, lambda: self.status_var.set(
                        f"✗ Build failed for {subsystem.get('name', subsystem_id)}"))
                    self.root.after(0, lambda: messagebox.showerror("Build Failed", 
                        f"Build failed:\n\n{error_msg}"))
            except Exception as e:
                self.root.after(0, lambda: self.status_var.set(f"Error: {e}"))
                self.root.after(0, lambda: messagebox.showerror("Error", f"Failed to build: {e}"))
        
        thread = threading.Thread(target=build_thread, daemon=True)
        thread.start()
    
    def update_subsystem_details(self, subsystem_id):
        """Update subsystem details display"""
        subsystem = self.subsystems_data.get(subsystem_id, {})
        if not subsystem:
            return
        
        config_key = f"SUBSYSTEM_{subsystem_id}"
        enabled = self.config.get(config_key, 'n') == 'y'
        required = subsystem.get('required', False)
        
        self.details_text.config(state=tk.NORMAL)
        self.details_text.delete(1.0, tk.END)
        
        details = f"{subsystem.get('name', subsystem_id)}\n"
        details += "=" * len(subsystem.get('name', subsystem_id)) + "\n\n"
        details += f"Status: {'✓ Enabled' if enabled else '✗ Disabled'}\n"
        
        if required:
            details += f"Required: Yes (Essential for kernel operation)\n\n"
        else:
            details += f"Required: No (Optional subsystem)\n\n"
        
        details += f"Description:\n{subsystem.get('description', 'No description')}\n\n"
        
        # Get files for current architecture
        arch = self.current_architecture
        files_dict = subsystem.get('files', {})
        files = files_dict.get(arch, files_dict.get('x86-64', []))
        
        if files:
            details += f"Source Files ({len(files)}):\n"
            for f in files:
                details += f"  • {f}\n"
        else:
            details += f"Source Files: None (not available for {arch})\n"
        
        dependencies = subsystem.get('dependencies', [])
        if dependencies:
            details += f"\nDependencies:\n"
            for dep in dependencies:
                dep_enabled = self.config.get(f"SUBSYSTEM_{dep}", 'n') == 'y'
                details += f"  • {dep} {'✓' if dep_enabled else '✗'}\n"
        
        architectures = subsystem.get('architectures', [])
        if architectures:
            details += f"\nSupported Architectures: {', '.join(architectures)}\n"
        
        self.details_text.insert(1.0, details)
        self.details_text.config(state=tk.DISABLED)
    
    def apply_profile(self, profile_id):
        """Apply a predefined profile"""
        profile = self.profiles_data.get(profile_id, {})
        
        if not profile:
            messagebox.showerror("Error", f"Profile {profile_id} not found")
            return
        
        # Check if profile is enabled
        if not profile.get('enabled', True):
            messagebox.showinfo("Profile Unavailable", 
                              f"{profile.get('name', profile_id)} is not yet available.\n\n{profile.get('description', '')}")
            return
        
        # Confirm application
        if not messagebox.askyesno("Apply Profile", 
                                  f"Apply profile '{profile.get('name', profile_id)}'?\n\n"
                                  f"This will enable/disable subsystems according to the profile."):
            return
        
        # Update architecture
        arch = profile.get('architecture', 'x86-64')
        self.current_architecture = arch
        
        # Disable all subsystems first
        for subsystem_id in self.subsystems_data.keys():
            config_key = f"SUBSYSTEM_{subsystem_id}"
            self.config[config_key] = 'n'
        
        # Enable subsystems in profile
        profile_subsystems = profile.get('subsystems', [])
        for subsystem_id in profile_subsystems:
            config_key = f"SUBSYSTEM_{subsystem_id}"
            self.config[config_key] = 'y'
        
        # Reload UI
        self.load_kernel_options()
        
        # Update architecture diagram
        selected = [sid for sid in profile_subsystems]
        self.arch_canvas.set_selected_subsystems(selected)
        
        messagebox.showinfo("Profile Applied", 
                          f"Profile '{profile.get('name', profile_id)}' applied successfully.\n\n"
                          f"{len(profile_subsystems)} subsystem(s) enabled.")
    
    def load_kernel_options(self):
        """Load and display subsystem options"""
        # Clear checkbox frame
        if hasattr(self, 'checkbox_frame'):
            for widget in self.checkbox_frame.winfo_children():
                widget.destroy()
        else:
            return
        
        # Add subsystems to list
        self.subsystem_vars = {}
        for subsystem_id, subsystem in sorted(self.subsystems_data.items()):
            config_key = f"SUBSYSTEM_{subsystem_id}"
            enabled = self.config.get(config_key, 'n') == 'y'
            
            var = tk.BooleanVar(value=enabled)
            self.subsystem_vars[subsystem_id] = var
            
            # Create frame for each subsystem
            item_frame = ttk.Frame(self.checkbox_frame)
            item_frame.pack(fill='x', padx=5, pady=2)
            
            # Create checkbox
            checkbox = ttk.Checkbutton(
                item_frame,
                text=subsystem.get('name', subsystem_id),
                variable=var,
                command=lambda sid=subsystem_id: self.on_subsystem_toggle(sid)
            )
            checkbox.pack(side='left', anchor='w')
            
            # Add required indicator
            if subsystem.get('required', False):
                req_label = ttk.Label(item_frame, text="[Required - Essential]", 
                                     font=('Arial', 7, 'bold'), foreground='#F57C00')
                req_label.pack(side='left', padx=(5, 0))
            
            # Add description label
            desc_text = subsystem.get('description', '')
            if len(desc_text) > 60:
                desc_text = desc_text[:57] + "..."
            desc_label = ttk.Label(item_frame, text=desc_text, 
                                  font=('Arial', 8), foreground='gray')
            desc_label.pack(side='left', padx=(10, 0), anchor='w')
            
            # Store subsystem ID in frame for click handling
            item_frame.subsystem_id = subsystem_id
            # Bind frame click to show details (but don't interfere with checkbox)
            item_frame.bind('<Button-1>', lambda e, sid=subsystem_id: self.on_subsystem_frame_click(sid))
            # Don't bind checkbox Button-1 - let the command handle it
        
        # Update architecture diagram
        selected = [sid for sid in self.subsystems_data.keys() 
                   if self.config.get(f"SUBSYSTEM_{sid}", 'n') == 'y']
        self.arch_canvas.set_selected_subsystems(selected)
    
    def on_subsystem_frame_click(self, subsystem_id):
        """Handle click on subsystem frame (not checkbox)"""
        self.current_subsystem = subsystem_id
        self.update_subsystem_details(subsystem_id)
        # Highlight in diagram
        self.arch_canvas.highlight_subsystem(subsystem_id)
    
    def setup_ui(self):
        """Setup the user interface"""
        # Configure style
        style = ttk.Style()
        style.configure("Treeview", rowheight=25)
        
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(expand=True, fill='both')
        
        # Header
        header_frame = ttk.Frame(main_frame)
        header_frame.pack(fill='x', pady=(0, 10))
        
        ttk.Label(header_frame, text=f"IR0 Kernel Configuration {self.kernel_version}", 
                 font=('Helvetica', 14, 'bold')).pack(side='left')
        
        # About button
        about_btn = ttk.Button(header_frame, text="About IR0", command=self.show_about_dialog)
        about_btn.pack(side='right', padx=5)
        
        # Button frame
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill='x', pady=(0, 10))
        
        # Profile buttons
        profile_frame = ttk.LabelFrame(button_frame, text="Profiles", padding=5)
        profile_frame.pack(side='left', padx=5)
        
        self.profile_x86_btn = ttk.Button(profile_frame, text="x86-64 Generic", 
                                          command=lambda: self.apply_profile('x86-64-generic'))
        self.profile_x86_btn.pack(side='left', padx=2)
        
        self.profile_arm_btn = ttk.Button(profile_frame, text="ARM32 Generic", 
                                         command=lambda: self.apply_profile('arm32-generic'),
                                         state='disabled')
        self.profile_arm_btn.pack(side='left', padx=2)
        
        # Action buttons
        action_frame = ttk.Frame(button_frame)
        action_frame.pack(side='left', padx=10)
        
        self.save_button = ttk.Button(action_frame, text="Save Configuration", command=self.save_config)
        self.save_button.pack(side='left', padx=5)
        
        self.build_button = ttk.Button(action_frame, text="Build Selected Subsystems", 
                                      command=self.on_build_selected)
        self.build_button.pack(side='left', padx=5)
        
        self.clean_button = ttk.Button(action_frame, text="Clean Selected Subsystems", 
                                      command=self.on_clean_selected)
        self.clean_button.pack(side='left', padx=5)
        
        self.run_button = ttk.Button(action_frame, text="Run Kernel", 
                                     command=self.on_run_kernel)
        self.run_button.pack(side='left', padx=5)
        
        # Paned window for left (subsystems) and right (architecture)
        paned = ttk.PanedWindow(main_frame, orient=tk.HORIZONTAL)
        paned.pack(expand=True, fill='both')
        
        # Left pane - Subsystem list (smaller, more space for diagram)
        left_frame = ttk.Frame(paned, padding=5)
        paned.add(left_frame, weight=1)
        self.left_pane_content = left_frame
        
        ttk.Label(left_frame, text="Subsystems", font=('Helvetica', 11, 'bold')).pack(anchor='w', pady=(0, 5))
        
        # Create scrollable frame for checkboxes
        canvas_container = tk.Canvas(left_frame, highlightthickness=0)
        scrollbar = ttk.Scrollbar(left_frame, orient="vertical", command=canvas_container.yview)
        scrollable_frame = ttk.Frame(canvas_container)
        
        scrollable_frame.bind(
            "<Configure>",
            lambda e: canvas_container.configure(scrollregion=canvas_container.bbox("all"))
        )
        
        canvas_container.create_window((0, 0), window=scrollable_frame, anchor="nw")
        canvas_container.configure(yscrollcommand=scrollbar.set)
        
        canvas_container.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Store reference for checkbox frame
        self.checkbox_frame = scrollable_frame
        
        # Placeholder for subsystem_list (kept for compatibility)
        self.subsystem_list = None
        
        # Right pane - Architecture diagram and details (larger)
        right_frame = ttk.Frame(paned, padding=5)
        paned.add(right_frame, weight=3)
        
        # Architecture diagram
        arch_label_frame = ttk.LabelFrame(right_frame, text="Kernel Architecture", padding=5)
        arch_label_frame.pack(fill='both', expand=True, pady=(0, 5))
        
        self.arch_canvas = KernelArchitectureCanvas(
            arch_label_frame, 
            self.subsystems_data, 
            self.architecture_data,
            on_subsystem_click=self.on_subsystem_click_arch
        )
        
        # Details frame
        details_label_frame = ttk.LabelFrame(right_frame, text="Subsystem Details", padding=5)
        details_label_frame.pack(fill='both', expand=False, pady=(5, 0))
        
        self.details_text = scrolledtext.ScrolledText(details_label_frame, wrap=tk.WORD, 
                                                      height=8, font=('Courier', 9), state=tk.DISABLED)
        self.details_text.pack(expand=True, fill='both')
        
        # Build button for selected subsystem
        build_frame = ttk.Frame(details_label_frame)
        build_frame.pack(fill='x', pady=(5, 0))
        
        self.build_subsystem_btn = ttk.Button(build_frame, text="Build This Subsystem", 
                                             command=self.on_build_current_subsystem)
        self.build_subsystem_btn.pack(side='left', padx=2)
        
        # Status bar
        self.status_var = tk.StringVar()
        self.status_var.set("Ready")
        status_bar = ttk.Label(main_frame, textvariable=self.status_var, relief=tk.SUNKEN, 
                             anchor=tk.W, padding=3)
        status_bar.pack(fill='x', pady=(5, 0))
        
        self.current_subsystem = None
    
    def on_subsystem_select(self, event):
        """Handle subsystem selection (legacy, not used with new UI)"""
        pass
    
    def on_build_current_subsystem(self):
        """Build currently selected subsystem"""
        if self.current_subsystem:
            self.build_subsystem(self.current_subsystem)
        else:
            messagebox.showwarning("No Selection", "Please select a subsystem first")
    
    def on_build_selected(self):
        """Build all selected subsystems"""
        selected_subsystems = [sid for sid in self.subsystems_data.keys() 
                              if self.config.get(f"SUBSYSTEM_{sid}", 'n') == 'y']
        
        if not selected_subsystems:
            messagebox.showwarning("No Selection", "No subsystems are enabled")
            return
        
        if not messagebox.askyesno("Build Subsystems", 
                                  f"Build {len(selected_subsystems)} subsystem(s)?"):
            return
        
        self.status_var.set("Building subsystems...")
        
        def build_all_thread():
            script_dir = os.path.dirname(os.path.abspath(__file__))
            kernel_root = os.path.dirname(os.path.dirname(script_dir))
            unibuild_script = os.path.join(kernel_root, 'scripts', 'unibuild.sh')
            
            all_files = []
            arch = self.current_architecture
            for sid in selected_subsystems:
                subsystem = self.subsystems_data.get(sid, {})
                files_dict = subsystem.get('files', {})
                files = files_dict.get(arch, files_dict.get('x86-64', []))
                all_files.extend(files)
            
            if all_files:
                try:
                    shell_cmd = self.platform_commands.get_shell_cmd()
                    cmd = [shell_cmd, unibuild_script] + all_files
                    process = subprocess.Popen(
                        cmd,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                        universal_newlines=True,
                        cwd=kernel_root
                    )
                    
                    output_lines = []
                    for line in process.stdout:
                        output_lines.append(line)
                        print(line.strip())
                    
                    process.wait()
                    
                    if process.returncode == 0:
                        self.root.after(0, lambda: self.status_var.set(
                            f"✓ Built {len(selected_subsystems)} subsystem(s) successfully"))
                        self.root.after(0, lambda: messagebox.showinfo("Build Success", 
                            f"Built {len(selected_subsystems)} subsystem(s) successfully!"))
                    else:
                        error_msg = '\n'.join(output_lines[-10:])
                        self.root.after(0, lambda: self.status_var.set("✗ Build failed"))
                        self.root.after(0, lambda: messagebox.showerror("Build Failed", 
                            f"Build failed:\n\n{error_msg}"))
                except Exception as e:
                    self.root.after(0, lambda: self.status_var.set(f"Error: {e}"))
                    self.root.after(0, lambda: messagebox.showerror("Error", f"Failed to build: {e}"))
        
        thread = threading.Thread(target=build_all_thread, daemon=True)
        thread.start()
    
    def on_clean_selected(self):
        """Clean object files for selected subsystems"""
        selected_subsystems = [sid for sid in self.subsystems_data.keys() 
                              if self.config.get(f"SUBSYSTEM_{sid}", 'n') == 'y']
        
        if not selected_subsystems:
            messagebox.showwarning("No Selection", "No subsystems are enabled")
            return
        
        if not messagebox.askyesno("Clean Subsystems", 
                                  f"Clean object files for {len(selected_subsystems)} subsystem(s)?"):
            return
        
        self.status_var.set("Cleaning subsystems...")
        
        def clean_all_thread():
            script_dir = os.path.dirname(os.path.abspath(__file__))
            kernel_root = os.path.dirname(os.path.dirname(script_dir))
            unibuild_clean_script = os.path.join(kernel_root, 'scripts', 'unibuild-clean.sh')
            
            all_files = []
            arch = self.current_architecture
            for sid in selected_subsystems:
                subsystem = self.subsystems_data.get(sid, {})
                files_dict = subsystem.get('files', {})
                files = files_dict.get(arch, files_dict.get('x86-64', []))
                all_files.extend(files)
            
            cleaned_count = 0
            if all_files:
                try:
                    for source_file in all_files:
                        # Only clean .c and .asm files (skip .o files)
                        if source_file.endswith('.c') or source_file.endswith('.asm'):
                            shell_cmd = self.platform_commands.get_shell_cmd()
                            cmd = [shell_cmd, unibuild_clean_script, source_file]
                            process = subprocess.Popen(
                                cmd,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                                universal_newlines=True,
                                cwd=kernel_root
                            )
                            stdout, stderr = process.communicate()
                            if process.returncode == 0:
                                cleaned_count += 1
                                print(f"Cleaned: {source_file}")
                    
                    self.root.after(0, lambda: self.status_var.set(
                        f"✓ Cleaned {cleaned_count} file(s) successfully"))
                    self.root.after(0, lambda: messagebox.showinfo("Clean Success", 
                        f"Cleaned {cleaned_count} object file(s) successfully!"))
                except Exception as e:
                    self.root.after(0, lambda: self.status_var.set(f"Error: {e}"))
                    self.root.after(0, lambda: messagebox.showerror("Error", f"Failed to clean: {e}"))
            else:
                self.root.after(0, lambda: self.status_var.set("No files to clean"))
        
        thread = threading.Thread(target=clean_all_thread, daemon=True)
        thread.start()
    
    def on_run_kernel(self):
        """Run the kernel using make run"""
        if not messagebox.askyesno("Run Kernel", 
                                  "Run the kernel in QEMU?\n\nThis will execute 'make run'."):
            return
        
        self.status_var.set("Starting kernel...")
        self.run_button.config(state='disabled')
        
        def run_thread():
            script_dir = os.path.dirname(os.path.abspath(__file__))
            kernel_root = os.path.dirname(os.path.dirname(script_dir))
            
            try:
                # Run make run using platform-specific make command
                make_cmd = self.platform_commands.get_make_cmd()
                process = subprocess.Popen(
                    [make_cmd, 'run'],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    universal_newlines=True,
                    cwd=kernel_root,
                    bufsize=1
                )
                
                # Show output in a window
                self.root.after(0, lambda: self.show_run_output(process))
                
            except Exception as e:
                self.root.after(0, lambda: self.status_var.set(f"Error: {e}"))
                self.root.after(0, lambda: messagebox.showerror("Error", f"Failed to run kernel: {e}"))
                self.root.after(0, lambda: self.run_button.config(state='normal'))
        
        thread = threading.Thread(target=run_thread, daemon=True)
        thread.start()
    
    def show_run_output(self, process):
        """Show QEMU output in a window"""
        output_win = tk.Toplevel(self.root)
        output_win.title("Kernel Output (QEMU)")
        output_win.geometry("800x600")
        
        text = scrolledtext.ScrolledText(output_win, wrap=tk.WORD, font=('Courier', 9))
        text.pack(expand=True, fill='both', padx=5, pady=5)
        
        text.insert(tk.END, "Starting kernel in QEMU...\n")
        text.insert(tk.END, "=" * 50 + "\n\n")
        
        def update_output():
            try:
                if process.poll() is None:
                    # Process still running
                    line = process.stdout.readline()
                    if line:
                        text.insert(tk.END, line)
                        text.see(tk.END)
                        text.update_idletasks()
                        output_win.after(100, update_output)
                    else:
                        output_win.after(100, update_output)
                else:
                    # Process finished
                    remaining = process.stdout.read()
                    if remaining:
                        text.insert(tk.END, remaining)
                    text.insert(tk.END, "\n" + "=" * 50 + "\n")
                    text.insert(tk.END, f"Process finished with return code {process.returncode}\n")
                    text.see(tk.END)
                    self.run_button.config(state='normal')
                    self.status_var.set("Kernel execution finished")
            except Exception as e:
                text.insert(tk.END, f"\nError reading output: {e}\n")
                self.run_button.config(state='normal')
                self.status_var.set(f"Error: {e}")
        
        def on_close():
            if process.poll() is None:
                if messagebox.askyesno("Kernel Running", "Kernel is still running. Terminate QEMU?"):
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except:
                        process.kill()
            output_win.destroy()
            self.run_button.config(state='normal')
        
        output_win.protocol("WM_DELETE_WINDOW", on_close)
        output_win.after(100, update_output)
    
    def show_about_dialog(self):
        """Show About dialog with kernel information and optional image"""
        about_win = tk.Toplevel(self.root)
        about_win.title("About IR0 Kernel")
        about_win.geometry("700x600")
        about_win.resizable(False, False)
        
        # Center dialog
        about_win.transient(self.root)
        about_win.grab_set()
        
        # Main container with scrollbar
        main_container = ttk.Frame(about_win)
        main_container.pack(expand=True, fill='both', padx=10, pady=10)
        
        # Try to load image
        image_label = None
        script_dir = os.path.dirname(os.path.abspath(__file__))
        image_paths = [
            os.path.join(script_dir, 'assets', 'black_hole_logo.png'),  # User's logo
            os.path.join(script_dir, 'assets', 'kernel_logo.png'),
            os.path.join(script_dir, 'assets', 'ir0_architecture.png'),
            os.path.join(script_dir, 'assets', 'logo.png'),
        ]
        
        image_loaded = False
        for image_path in image_paths:
            if os.path.exists(image_path):
                try:
                    # Try using PIL if available
                    try:
                        from PIL import Image, ImageTk
                        img = Image.open(image_path)
                        # Resize if too large
                        max_width, max_height = 600, 300
                        img.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
                        photo = ImageTk.PhotoImage(img)
                        image_label = ttk.Label(main_container, image=photo)
                        image_label.image = photo  # Keep reference
                        image_label.pack(pady=(0, 10))
                        image_loaded = True
                        break
                    except ImportError:
                        # Fallback to tkinter PhotoImage (PNG/GIF only)
                        if image_path.lower().endswith(('.png', '.gif')):
                            photo = tk.PhotoImage(file=image_path)
                            # Subsample if too large
                            if photo.width() > 600 or photo.height() > 300:
                                factor = max(photo.width() // 600, photo.height() // 300, 1)
                                photo = photo.subsample(factor, factor)
                            image_label = ttk.Label(main_container, image=photo)
                            image_label.image = photo
                            image_label.pack(pady=(0, 10))
                            image_loaded = True
                            break
                except Exception as e:
                    print(f"Could not load image {image_path}: {e}")
        
        # Header
        header = ttk.Label(main_container, text="IR0 Kernel", 
                          font=('Helvetica', 18, 'bold'))
        header.pack(pady=(0 if image_loaded else 10, 5))
        
        version_label = ttk.Label(main_container, text=f"Version {self.kernel_version}", 
                                 font=('Helvetica', 11))
        version_label.pack(pady=(0, 15))
        
        # Scrollable text area for description
        text_frame = ttk.Frame(main_container)
        text_frame.pack(expand=True, fill='both')
        
        scrollbar = ttk.Scrollbar(text_frame)
        scrollbar.pack(side='right', fill='y')
        
        info_text = tk.Text(text_frame, wrap=tk.WORD, height=15, 
                           font=('Arial', 10), yscrollcommand=scrollbar.set,
                           relief=tk.FLAT, bg='#f5f5f5')
        info_text.pack(side='left', expand=True, fill='both')
        scrollbar.config(command=info_text.yview)
        
        # Kernel information content
        info_content = """IR0 Kernel

IR0 is a monolithic operating system kernel designed for x86-64 architecture, with planned support for ARM32. The kernel is built from the ground up to demonstrate fundamental OS concepts while maintaining a clean, modular architecture.

Architecture Overview:

The IR0 kernel follows a layered architecture:

• Hardware Layer: Low-level architecture-specific code (GDT, IDT, interrupt handling)
• Memory Layer: Physical and virtual memory management, paging, heap allocation
• Kernel Core: Process management, scheduling, system calls, and core services
• Device Layer: Hardware drivers (PS/2, serial, ATA, timers, video, audio)
• Storage Layer: Virtual File System (VFS), MINIX filesystem, disk partitions

Key Features:

Memory Management:
- Paging with identity mapping for kernel space
- Dynamic heap allocation (kmalloc/kfree)
- Physical memory bitmap management
- Kernel and user space separation with ring 0/3 protection

Process Management:
- Complete process state machine (NEW, READY, RUNNING, SLEEPING, ZOMBIE, DEAD)
- Multiple scheduling algorithms (Round-Robin, Priority-based, CFS)
- Process tree hierarchy with parent-child relationships
- Context switching with full register preservation

System Calls:
- POSIX-compatible system call interface via syscall/sysret (x86-64)
- Process control: fork, exec, exit, wait, getpid
- File operations: open, close, read, write, lseek
- Memory management: brk, mmap, munmap, mprotect
- Time operations: time, gettimeofday, sleep, usleep

File System:
- Virtual File System (VFS) abstraction layer
- MINIX filesystem support for persistent storage
- RAM filesystem for boot files and temporary storage
- Mount point management with unified namespace

Hardware Support:
- PS/2 keyboard and mouse drivers
- ATA/IDE storage controller
- VGA text mode and VBE graphics
- Multiple timer sources (PIT, HPET, LAPIC)
- Serial port communication for debugging
- Sound Blaster 16 audio (experimental)
- DMA controller support

Development Tools:
- Interactive shell with built-in commands
- Configuration menu system (this tool)
- Incremental compilation with unibuild
- Cross-platform build support (Linux, Windows/MinGW)
- Comprehensive debugging infrastructure

Build System:
- Native Linux compilation
- Windows cross-compilation with MinGW-w64
- Modular subsystem selection
- Dependency checking and automated testing
- Future support planned for Rust and C++ components

The IR0 kernel serves as both an educational project demonstrating operating system internals and a platform for experimenting with OS design concepts.

For more information, documentation, and source code:
- GitHub Repository: https://github.com/IRodriguez13/IR0
- Documentation Wiki: https://ir0-wiki.netlify.app/
"""
        
        info_text.insert('1.0', info_content)
        info_text.config(state=tk.DISABLED)
        
        links_frame.pack(pady=(10, 0))
        
        # Repository link
        def open_repository():
            import webbrowser
            webbrowser.open('https://github.com/IRodriguez13/IR0')
        
        repo_btn = ttk.Button(links_frame, text="📁 GitHub Repository", command=open_repository)
        repo_btn.pack(side='left', padx=5)
        
        # Wiki link
        def open_wiki():
            import webbrowser
            webbrowser.open('https://ir0-wiki.netlify.app/')
        
        wiki_btn = ttk.Button(links_frame, text="📚 Documentation Wiki", command=open_wiki)
        wiki_btn.pack(side='left', padx=5)
        
        # Close button
        close_btn = ttk.Button(main_container, text="Close", command=about_win.destroy)
        close_btn.pack(pady=(10, 0))



def main():
    # Change to kernel root directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    kernel_root = os.path.dirname(os.path.dirname(script_dir))
    os.chdir(kernel_root)
    
    root = tk.Tk()
    app = KernelConfigGUI(root)
    
    # Center the window
    window_width = 1800
    window_height = 1000
    screen_width = root.winfo_screenwidth()
    screen_height = root.winfo_screenheight()
    center_x = int(screen_width/2 - window_width/2)
    center_y = int(screen_height/2 - window_height/2)
    root.geometry(f'{window_width}x{window_height}+{center_x}+{center_y}')
    
    root.mainloop()


if __name__ == "__main__":
    main()
