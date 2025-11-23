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
            "shell": "bash",
            "path_sep": "\\"
        },
        "linux": {
            "make": "make",
            "python": "python3",
            "qemu": "qemu-system-x86_64",
            "shell": "sh",
            "path_sep": "/"
        },
        "macos": {
            "make": "make",
            "python": "python3",
            "qemu": "qemu-system-x86_64",
            "shell": "sh",
            "path_sep": "/"
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
    
    def get_path_sep(self):
        return self.commands["path_sep"]


class PlatformSelectorDialog:
    """Dialog for selecting build platform"""
    
    def __init__(self, parent):
        self.result = None
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Select Build Platform")
        self.dialog.geometry("450x300")
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
            ("windows", "Windows", "Use mingw32-make, python, qemu-system-x86_64w.exe"),
            ("linux", "Linux", "Use make, python3, qemu-system-x86_64"),
            ("macos", "macOS", "Use make, python3, qemu-system-x86_64")
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
                     font=('Helvetica', 8)).pack(side='left', padx=(10, 0))
        
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


# ===============================================================================
# KERNEL ARCHITECTURE CANVAS
# ===============================================================================

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
        canvas_width = self.canvas.winfo_width() if self.canvas.winfo_width() > 1 else 1200
        canvas_height = max(800, len(self.architecture_data['layers']) * 140)
        
        # Set canvas scroll region
        self.canvas.config(scrollregion=(0, 0, canvas_width, canvas_height))
        
        # Draw layers (larger for better visibility)
        layer_width = canvas_width - 40
        layer_height = 120
        layer_spacing = 25
        start_x = 20
        start_y = 20
        
        for layer_idx, layer in enumerate(self.architecture_data['layers']):
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
                subsystem_width = (layer_width - 30) // num_subsystems
                subsystem_spacing = 10
                
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
                    
                    # Draw subsystem box
                    rect_id = self.canvas.create_rectangle(
                        x_pos, y_pos + 35, x_pos + subsystem_width - subsystem_spacing, y_pos + layer_height - 10,
                        fill=fill_color, outline=outline_color, width=outline_width,
                        tags=('subsystem', subsystem_id)
                    )
                    
                    self.subsystem_rects[subsystem_id] = rect_id
                    self.subsystem_positions[subsystem_id] = (x_pos, y_pos + 35, 
                                                               x_pos + subsystem_width - subsystem_spacing, 
                                                               y_pos + layer_height - 10)
                    
                    # Draw subsystem name (larger font)
                    self.canvas.create_text(
                        x_pos + (subsystem_width - subsystem_spacing) // 2, y_pos + 60,
                        text=subsystem['name'], anchor='center',
                        font=('Arial', 10, 'bold'), fill='#000000', tags=('subsystem', subsystem_id)
                    )
                    
                    # Draw required indicator
                    if subsystem.get('required', False):
                        self.canvas.create_text(
                            x_pos + (subsystem_width - subsystem_spacing) // 2, y_pos + 85,
                            text='[Required]', anchor='center',
                            font=('Arial', 8), fill='#666666', tags=('subsystem', subsystem_id)
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
