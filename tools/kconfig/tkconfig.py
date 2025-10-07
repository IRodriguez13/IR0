import os
import sys
import json
import threading
import subprocess

# Verify dependencies
try:
    import tkinter as tk
    from tkinter import ttk, messagebox, scrolledtext
except ImportError as e:
    print(f"Error: {e}. Please install python3-tk package.")
    sys.exit(1)

class ConfigApp:
    def __init__(self, root):
        self.root = root
        self.root.title("IR0 Kernel Configuration")
        
        # Set window size and position
        window_width = 900
        window_height = 700
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()
        center_x = int(screen_width/2 - window_width/2)
        center_y = int(screen_height/2 - window_height/2)
        self.root.geometry(f'{window_width}x{window_height}+{center_x}+{center_y}')
        
        # Initialize paths
        self.kernel_path = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        self.config_file = os.path.join(self.kernel_path, '.config')
        
        # Create .config if it doesn't exist
        if not os.path.exists(self.config_file):
            default_config = os.path.join(self.kernel_path, '.config.default')
            if os.path.exists(default_config):
                import shutil
                shutil.copyfile(default_config, self.config_file)
                self.log(f"Created new config file from {default_config}")
            else:
                # Create empty config
                with open(self.config_file, 'w') as f:
                    f.write("# IR0 Kernel Configuration\n")
                self.log("Created new empty config file")
        
        # Initialize config data
        self.config_data = {}
        self.modules = {}
        self.subsystem_descriptions = {}
        
        # Configuration text
        self.texts = {
            'window_title': 'IR0 Kernel Configuration',
            'save': 'Save',
            'compile': 'Compile',
            'exit': 'Exit',
            'select_option': 'Select an option to see its description',
            'save_error': 'Error saving configuration',
            'confirm_exit': 'Do you want to save changes before exiting?',
            'compiling': 'Compiling...',
            'module_desc': 'Enables the {item} module from {dir_name} directory',
            'enable_module': 'Enable {item} ({dir_name})',
            'enabled': 'Enabled',
            'disabled': 'Disabled',
            'Status': 'Status',
            'Category': 'Category',
            'no_description': 'No description available.',
            'config_saved': 'Configuration saved successfully to .config'
        }
        
        print("Starting graphical configuration menu...")
        
        # First load configuration and scan modules
        self.load_config()
        self.scan_kernel_modules()
        
        # Then create the interface with the loaded data
        self.create_widgets()
        
    def on_select(self, event=None):
        """Handle item selection in the tree"""
        selected = self.tree.selection()
        if not selected:
            return
            
        item = selected[0]
        item_text = self.tree.item(item, 'text')
        item_values = self.tree.item(item, 'values')
        
        # Clear the description area
        self.desc_text.config(state=tk.NORMAL)
        self.desc_text.delete(1.0, tk.END)
        
        # Show selected item information
        if item_values and len(item_values) > 0:
            config_key = item_values[0]
            if config_key in self.config_data:
                config = self.config_data[config_key]
                
                # Get status text
                status_text = 'Enabled' if config.get('value', False) else 'Disabled'
                
                # Get module description if it's a module
                help_text = config.get('help', 'No description available.')
                if 'MODULE_' in config_key and isinstance(help_text, dict):
                    help_text = help_text.get('en', 'No description available.')
                
                # Show subsystem information if available
                subsystem = config.get('subsystem', '')
                if subsystem and hasattr(self, 'subsystem_descriptions') and subsystem in self.subsystem_descriptions:
                    self.desc_text.insert(tk.END, f"=== {subsystem.upper()} ===\n\n")
                    if isinstance(self.subsystem_descriptions[subsystem], dict):
                        desc = self.subsystem_descriptions[subsystem].get(self.current_lang, 
                                                                       self.subsystem_descriptions[subsystem].get('en', ''))
                        self.desc_text.insert(tk.END, f"{desc}\n\n")
                    else:
                        self.desc_text.insert(tk.END, f"{self.subsystem_descriptions[subsystem]}\n\n")
                    self.desc_text.insert(tk.END, "-"*50 + "\n\n")
                
                prompt = config.get('prompt', '')
                if prompt:
                    self.desc_text.insert(tk.END, f"{prompt}\n\n")
                
                # Mostrar estado traducido
                # Show option information
                self.desc_text.insert(tk.END, f"Option: {config_key}\n")
                self.desc_text.insert(tk.END, f"Status: {status_text}\n\n")
                
                # Show description
                if help_text:
                    self.desc_text.insert(tk.END, f"{help_text}\n\n")
                
                # Show dependencies if they exist
                depends = config.get('depends', [])
                if depends:
                    self.desc_text.insert(tk.END, "Depends on:\n")
                    for dep in depends:
                        self.desc_text.insert(tk.END, f"- {dep}\n")
                    self.desc_text.insert(tk.END, "\n")
                
                # Show current selection status
                if config.get('value', False):
                    self.desc_text.insert(tk.END, "This option is currently ENABLED.\n")
                else:
                    self.desc_text.insert(tk.END, "This option is currently DISABLED.\n")
                
                # Add instructions
                self.desc_text.insert(tk.END, "\nPress SPACE or ENTER to toggle this option.")
            else:
                self.desc_text.insert(tk.END, "No configuration data available for this item.")
        else:
            self.desc_text.insert(tk.END, "Select an option to see its description.")
        
        # Disable editing
        self.desc_text.config(state=tk.DISABLED)
        
    def scan_kernel_modules(self):
        """Scan kernel directories for modules"""
        self.modules = {}
        
        # Define the module categories and their paths
        module_categories = {
            'drivers': {
                'path': 'drivers',
                'subdirs': {
                    'timer': 'Timers and clocks',
                    'storage': 'Storage devices',
                    'IO': 'Input/Output',
                    'video': 'Video and display',
                    'audio': 'Audio devices',
                    'serial': 'Serial communication',
                    'dma': 'Direct Memory Access',
                }
            },
            'fs': {
                'path': 'fs',
                'description': 'File systems',
                'modules': ['vfs', 'minix_fs']
            },
            'kernel': {
                'path': 'kernel',
                'description': 'Core kernel functionality',
                'modules': ['scheduler', 'process', 'memory']
            },
            'interrupt': {
                'path': 'interrupt',
                'description': 'Interrupt handling',
                'modules': ['pic', 'idt', 'isr_handlers']
            },
            'arch': {
                'path': 'arch',
                'description': 'Architecture-specific code',
                'subdirs': {
                    'x86-64': 'x86-64 Architecture',
                    'x86-32': 'x86-32 Architecture',
                    'arm-32': 'ARM-32 Architecture'
                }
            }
        }
        
        for category, cat_info in module_categories.items():
            category_path = os.path.join(self.kernel_path, cat_info['path'])
            
            if not os.path.exists(category_path):
                continue
                
            self.modules[category] = []
            
            # Add category description
            if category not in self.subsystem_descriptions:
                self.subsystem_descriptions[category] = cat_info.get('description', category)
            
            # Handle subdirectories
            if 'subdirs' in cat_info:
                for subdir, subdesc in cat_info['subdirs'].items():
                    subdir_path = os.path.join(category_path, subdir)
                    if os.path.exists(subdir_path):
                        # Add the subdirectory as a module
                        self.modules[category].append(subdir)
                        
                        # Add configuration for this module if it doesn't exist
                        config_name = f"MODULE_{category.upper()}_{subdir.upper()}"
                        if config_name not in self.config_data:
                            module_desc = self.get_module_description(subdir)
                            self.config_data[config_name] = {
                                'type': 'bool',
                                'value': False,
                                'help': module_desc,
                                'prompt': f'Enable {subdir} {category}',
                                'subsystem': category
                            }
            
            # Handle direct modules
            if 'modules' in cat_info:
                for module in cat_info['modules']:
                    self.modules[category].append(module)
                    
                    # Add configuration for this module if it doesn't exist
                    config_name = f"MODULE_{category.upper()}_{module.upper()}"
                    if config_name not in self.config_data:
                        module_desc = self.get_module_description(module)
                        self.config_data[config_name] = {
                            'type': 'bool',
                            'value': False,
                            'help': module_desc,
                            'prompt': f'Enable {module} {category}',
                            'subsystem': category
                        }
    
    def get_module_description(self, module_name, lang='en'):
        """Get detailed description for a module"""
        descriptions = {
            # Timer modules
            'TIMER': {
                'en': """Timer Subsystem

Provides system timing and scheduling services.

Features:
- High Precision Event Timer (HPET) support
- Programmable Interval Timer (PIT)
- Local APIC timer
- Real Time Clock (RTC) integration"""
            },
            'HPET': {
                'en': """HPET Timer

High Precision Event Timer driver.

Features:
- High-resolution timing
- Multiple timer channels
- Precise event scheduling
- Low power consumption"""
            },
            'PIT': {
                'en': """Programmable Interval Timer

Legacy PIT (8254) timer support.

Features:
- System timer functionality
- Speaker control
- Legacy compatibility
- Precise interval timing"""
            },
            'RTC': {
                'en': """Real Time Clock

Hardware real-time clock support.

Features:
- Date and time keeping
- Battery-backed operation
- Alarm functionality
- Periodic interrupts"""
            },
            'LAPIC': {
                'en': """Local APIC Timer

Advanced Programmable Interrupt Controller timer.

Features:
- Per-CPU timers
- High resolution
- Power management support
- Precise CPU-local timing"""
            },
            # Storage modules
            'ATA': {
                'en': """ATA/ATAPI Driver

Supports ATA/ATAPI storage devices.

Features:
- PIO mode
- DMA support (if available)
- ATAPI CD/DVD support
- SATA compatibility"""
            },
            # Input/Output
            'PS2': {
                'en': """PS/2 Controller

PS/2 keyboard and mouse controller.

Features:
- Keyboard input
- Mouse input
- Interrupt-driven operation
- Configurable scan codes"""
            },
            'SERIAL': {
                'en': """Serial Port

UART serial port driver.

Features:
- 16550A UART support
- Configurable baud rates
- Interrupt-driven I/O
- Debug console support"""
            },
            # Filesystems
            'VFS': {
                'en': """Virtual File System

Abstracts various filesystem types.

Features:
- Unified filesystem interface
- Multiple FS support
- File descriptor management
- Mount point handling"""
            },
            'MINIX_FS': {
                'en': """MINIX Filesystem

MINIX 3 filesystem implementation.

Features:
- Journaling support
- POSIX compliance
- Efficient small file storage
- Fast directory operations"""
            },
            # Core kernel
            'SCHEDULER': {
                'en': """Process Scheduler

Kernel task scheduler.

Features:
- Completely Fair Scheduler (CFS)
- Priority-based scheduling
- Real-time support
- Load balancing"""
            },
            'MEMORY': {
                'en': """Memory Management

Physical and virtual memory management.

Features:
- Paging
- Memory allocation
- Virtual memory
- Memory protection"""
            },
            'AUDIO': {
                'en': """Audio Subsystem

Provides support for audio devices and sound processing.

Features:
- Support for common audio formats
- Volume control and mixing
- Low-latency playback
- Multiple audio streams

Dependencies:
- DMA controller
- Interrupt controller
- Memory management"""
            }

Características:
- Soporte básico para reproducción de audio
- Capa de abstracción de dispositivos de audio
- Soporte para formatos de audio comunes
- Control de volumen y mezcla

Dependencias:
- Controlador DMA
- Controlador de interrupciones
- Gestión de memoria"""
            },
            'VIDEO': {
                'en': """Video Subsystem

Manages display devices and graphics rendering.

Features:
- Framebuffer support
- Basic 2D acceleration
- Multiple display support
- Resolution and color depth management""",
                'es': """Subsistema de Video

Gestiona dispositivos de visualización y renderizado de gráficos.

Características:
- Soporte para framebuffer
- Aceleración 2D básica
- Soporte para múltiples pantallas
- Gestión de resolución y profundidad de color"""
            },
            'NETWORK': {
                'en': """Networking Stack

Implements network protocols and device drivers.

Features:
- TCP/IP protocol stack
- Ethernet support
- Socket API
- Basic network utilities""",
                'es': """Pila de Red

Implementa protocolos de red y controladores de dispositivos.

Características:
- Pila de protocolos TCP/IP
- Soporte Ethernet
- API de sockets
- Utilidades básicas de red"""
            }
        }
        
        # Find module description
        for module, desc in descriptions.items():
            if module in module_name.upper():
                return desc.get(lang, desc['en'])
        
        # Default description
        default_desc = {
            'en': f"Module {module_name}. No detailed description available.",
            'es': f"Módulo {module_name}. No hay descripción detallada disponible."
        }
        return default_desc[lang]
    def load_config(self):
        """Load configuration from Config.in"""
        current_section = None
        
        # Detailed subsystem descriptions
        self.subsystem_descriptions = {
            'scheduler': "Kernel scheduling system. Controls how CPU time is allocated to processes.\n\nAvailable algorithms:\n- CFS (Completely Fair Scheduler): Balances CPU time fairly\n- Round Robin: Assigns fixed time slices to each process in circular order\n- Priority: Executes processes based on their priority level",
            'memory': "Memory management system. Handles memory allocation and deallocation.\n\nIncludes:\n- Physical page management\n- Dynamic memory allocation\n- Memory swapping (swap)",
            'fs': "File system. Provides access to data on storage devices.\n\nSupported:\n- EXT4\n- BTRFS\n- NTFS (read only)",
            'net': "Network stack. Handles network communication.\n\nSupported protocols:\n- TCP/IP\n- UDP\n- ICMP\n- DHCP",
            'drivers': "Device drivers. Enable hardware communication.\n\nIncludes drivers for:\n- Hard drives\n- Network cards\n- USB ports"
        }
        
        try:
            if os.path.exists(self.config_file):
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    current_section = None
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith('#'):
                            continue
                            
                        if line.startswith('config '):
                            current_section = line[7:].strip()
                            self.config_data[current_section] = {
                                'type': 'bool',
                                'value': False,
                                'help': '',
                                'prompt': ''
                            }
                        elif line.startswith('bool') and current_section:
                            self.config_data[current_section]['prompt'] = line[5:].strip('"')
                        elif line.startswith('default') and current_section:
                            self.config_data[current_section]['value'] = 'y' in line.lower()
                        elif line.startswith('help') and current_section:
                            # Here we could capture help text if needed
                            pass
        except Exception as e:
            messagebox.showerror("Error", f"Error loading configuration: {e}")
    
    def save_config(self):
        """Save configuration to .config"""
        try:
            with open('.config', 'w') as f:
                for key, data in self.config_data.items():
                    f.write(f"{key}={1 if data['value'] else 0}\n")
            messagebox.showinfo(self.texts['save'], self.texts['config_saved'])
            return True
        except Exception as e:
            messagebox.showerror(self.texts['save_error'], f"{self.texts['save_error']}: {e}")
            return False
    
    def start_compile(self):
        """Start the kernel compilation process"""
        # First save the configuration
        if not self.save_config():
            return
            
        # Clear the console
        self.console.delete(1.0, tk.END)
        self.log(self.texts['compiling'])
        
        # Disable buttons during compilation
        for widget in self.root.winfo_children():
            if isinstance(widget, ttk.Button):
                widget.config(state=tk.DISABLED)
        
        # Start compilation in a separate thread
        self.compile_thread = threading.Thread(target=self.run_compile)
        self.compile_thread.daemon = True
        self.compile_thread.start()
        
        # Periodically check compilation status
        self.root.after(100, self.check_compile_status)
    
    def create_widgets(self):
        # Frame principal con paneles divididos
        paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True)
        # Left panel - Options tree
        left_frame = ttk.Frame(paned, padding=5)
        
        # Right panel - Description/help
        right_frame = ttk.Frame(paned, padding=5)
        
        # Add frames to panel
        paned.add(left_frame, weight=1)
        paned.add(right_frame, weight=1)
        
        # Treeview para mostrar las opciones
        self.tree = ttk.Treeview(left_frame, columns=('value'), show='tree', selectmode='browse')
        self.tree.heading('#0', text='Configuration Options')
        self.tree.column('#0', width=400)
        
        # Tree scrollbar
        yscroll = ttk.Scrollbar(left_frame, orient='vertical', command=self.tree.yview)
        self.tree.configure(yscrollcommand=yscroll.set)
        
        # Description area
        self.desc_text = scrolledtext.ScrolledText(
            right_frame, 
            wrap=tk.WORD, 
            width=40, 
            height=20,
            font=('Courier', 10),
            bg='#f0f0f0',
            padx=5,
            pady=5
        )
        self.desc_text.pack(fill=tk.BOTH, expand=True)
        
        # Frame for buttons
        button_frame = ttk.Frame(self.root)
        
        # Main buttons
        self.save_btn = ttk.Button(button_frame, text=self.texts['save'], command=self.save_config)
        self.compile_btn = ttk.Button(button_frame, text=self.texts['compile'], command=self.start_compile)
        self.exit_btn = ttk.Button(button_frame, text=self.texts['exit'], command=self.root.quit)
        
        # Pack main buttons
        self.save_btn.pack(side=tk.LEFT, padx=5, pady=5)
        self.compile_btn.pack(side=tk.LEFT, padx=5, pady=5)
        self.exit_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        # Output console
        self.console = scrolledtext.ScrolledText(
            self.root,
            height=10,
            wrap=tk.WORD,
            font=('Courier', 9),
            bg='black',
            fg='lime',
            insertbackground='white'
        )
        
        # Posicionamiento
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        yscroll.pack(side=tk.RIGHT, fill=tk.Y)
        
        paned.pack(fill=tk.BOTH, expand=True)
        button_frame.pack(fill=tk.X, pady=5)
        self.console.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Configure events
        self.tree.bind('<<TreeviewSelect>>', self.on_select)
        self.tree.bind('<Double-1>', self.toggle_value)
        
        # Load options into the tree
        self.load_options()
        
        # Show initial help
        self.desc_text.config(state=tk.NORMAL)
        self.desc_text.delete(1.0, tk.END)
        self.desc_text.insert(tk.END, "IR0 Kernel Configuration\n\n")
        self.desc_text.insert(tk.END, "Select an option to view its description.\n")
        self.desc_text.insert(tk.END, "Double-click to enable/disable an option.\n\n")
        self.desc_text.insert(tk.END, "Buttons:\n")
        self.desc_text.insert(tk.END, "- Save: Saves the current configuration\n")
        self.desc_text.insert(tk.END, "- Compile: Starts kernel compilation\n")
        self.desc_text.insert(tk.END, "- Exit: Closes the configurator")
        self.desc_text.config(state=tk.DISABLED)
    
    def load_options(self):
        """Load options into the tree"""
        # Make sure self.modules is initialized
        if not hasattr(self, 'modules'):
            self.modules = {}
            
        # Clear the tree
        for item in self.tree.get_children():
            self.tree.delete(item)
            
        if not self.modules:
            self.tree.insert('', 'end', text="No modules found. Please check your kernel source directory.")
        
        # First, add main categories
        categories = {}
        
        # Add kernel modules if they exist
        if hasattr(self, 'modules') and self.modules:
            for category, modules in self.modules.items():
                if modules:  # Only add categories that have modules
                    cat_id = self.tree.insert('', 'end', text=category.capitalize(), 
                                           tags=('category',))
                    categories[category] = cat_id
                
                # Add modules for this category
                for module in modules:
                    config_name = f"MODULE_{category.upper()}_{module.upper()}"
                    if config_name in self.config_data:
                        data = self.config_data[config_name]
                        value = "[✓]" if data.get('value', False) else "[ ]"
                        self.tree.insert(
                            cat_id, 'end', 
                            text=f"{value} {module}",
                            values=(config_name,),
                            tags=('module', config_name)
                        )
        
        # Add configurations that are not modules
        other_configs = [
            (key, data) for key, data in self.config_data.items() 
            if not key.startswith('MODULE_')
        ]
        
        if other_configs:
            other_id = self.tree.insert('', 'end', text="Other Configurations", 
                                     tags=('category',))
            
            for key, data in other_configs:
                value = "[✓]" if data.get('value', False) else "[ ]"
                self.tree.insert(
                    other_id, 'end',
                    text=f"{value} {data.get('prompt', key)}",
                    values=(key,),
                    tags=('config', key)
                )
        
            
        # Clear console
        self.console.delete(1.0, tk.END)
        self.log("Configuration loaded. Select the desired options.")
        
        # Show welcome message only
        self.log("Configuration loaded. Select the desired options.")
        self.log("Use the buttons to save the configuration or compile.")
    
    def run_compile(self):
        """Execute the compilation command"""
        try:
            self.compile_process = subprocess.Popen(
                ['make', 'ir0'],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                universal_newlines=True
            )
            
            # Leer la salida en tiempo real
            while True:
                if self.stop_compile:
                    self.compile_process.terminate()
                    break
                    
                output = self.compile_process.stdout.readline()
                if output == '' and self.compile_process.poll() is not None:
                    break
                if output:
                    self.log(output.strip())
            
            # Exit code
            return_code = self.compile_process.poll()
            if return_code == 0:
                self.log("\nCompilation completed successfully!")
            else:
                self.log(f"\nCompilation error (code {return_code})")
                
        except Exception as e:
            self.log(f"Error starting compilation: {str(e)}")
        finally:
            # Re-enable buttons
            self.root.after(100, self.enable_buttons)
    
    def check_compile_status(self):
        """Check compilation status"""
        if hasattr(self, 'compile_process') and self.compile_process.poll() is None:
            self.root.after(100, self.check_compile_status)
    
    def enable_buttons(self):
        """Re-enable buttons after compilation"""
        for widget in self.root.winfo_children():
            if isinstance(widget, ttk.Button):
                widget.config(state=tk.NORMAL)
    
    
    def on_closing(self):
        """Handle window close event"""
        if hasattr(self, 'compile_thread') and self.compile_thread.is_alive():
            if messagebox.askokcancel("Exit", "Compilation is in progress. Are you sure you want to exit?"):
                self.stop_compile = True
                self.root.destroy()
        else:
            if messagebox.askokcancel(self.texts['exit'], self.texts['confirm_exit']):
                if self.save_config():
                    self.root.destroy()
            else:
                self.root.destroy()
    
    def log(self, message):
        """Add a message to the output console"""
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, message + '\n')
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)
        self.console.update_idletasks()
    
    def toggle_value(self, event):
        """Toggle an option's value on double click"""
        item = self.tree.identify('item', event.x, event.y)
        if not item:
            return
            
        values = self.tree.item(item, 'values')
        if not values:  # It's a category
            return
            
        config_key = values[0]
        if config_key in self.config_data:
            # Toggle the value
            self.config_data[config_key]['value'] = not self.config_data[config_key]['value']
            
            # Update the display
            current_text = self.tree.item(item, 'text')
            if current_text.startswith('['):
                new_text = f"[✓] {current_text[4:]}" if self.config_data[config_key]['value'] else f"[ ] {current_text[4:]}"
                self.tree.item(item, text=new_text)
            
            # Update the description
            self.on_select(None)
            
            # Save the configuration
            self.save_config()

def main():
    root = tk.Tk()
    app = ConfigApp(root)
    
    # Configurar cierre seguro
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    
    # Centrar ventana
    window_width = 1000
    window_height = 700
    screen_width = root.winfo_screenwidth()
    screen_height = root.winfo_screenheight()
    x = (screen_width // 2) - (window_width // 2)
    y = (screen_height // 2) - (window_height // 2)
    root.geometry(f'{window_width}x{window_height}+{x}+{y}')
    
    # Disable resizing
    root.resizable(True, True)
    
    # Set icon if available
    try:
        root.iconbitmap('path/to/icon.ico')  # Replace with your icon path
    except:
        pass
    
    root.mainloop()

if __name__ == "__main__":
    main()
