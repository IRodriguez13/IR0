#!/usr/bin/env python3

import os
import sys
import curses
import re
from dataclasses import dataclass, field
from typing import List, Dict, Optional

@dataclass
class MenuEntry:
    type: str  # menu, config, etc.
    prompt: str
    name: str
    depends: List[str] = field(default_factory=list)
    default: bool = False
    help_text: str = ""
    visible: bool = True
    value: bool = False
    children: List['MenuEntry'] = field(default_factory=list)
    parent: Optional['MenuEntry'] = None

def parse_config(filename: str) -> MenuEntry:
    """Parse the configuration file and build the menu tree."""
    root = MenuEntry(type="menu", prompt="IR0 Kernel Configuration", name="")
    current_menu = root
    current_entry = None
    
    with open(filename, 'r') as f:
        lines = f.readlines()
    
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        
        if not line or line.startswith('#'):
            i += 1
            continue
            
        # Handle menu entries
        if line.startswith('menu '):
            prompt = line[5:].strip('"')
            menu = MenuEntry(type="menu", prompt=prompt, name=prompt.lower().replace(' ', '_'))
            menu.parent = current_menu
            current_menu.children.append(menu)
            current_menu = menu
            
        elif line == 'endmenu':
            if current_menu.parent:
                current_menu = current_menu.parent
                
        # Handle config entries
        elif line.startswith('config '):
            name = line[7:].strip()
            config = MenuEntry(type="config", prompt="", name=name)
            
            # Parse config properties
            while i + 1 < len(lines) and lines[i+1].startswith('    '):
                i += 1
                prop_line = lines[i].strip()
                
                if prop_line.startswith('bool '):
                    config.prompt = prop_line[5:].strip('\"')
                elif prop_line.startswith('default '):
                    config.default = 'y' in prop_line.lower()
                    config.value = config.default
                elif prop_line.startswith('help'):
                    help_text = []
                    while i + 1 < len(lines) and lines[i+1].startswith('      '):
                        i += 1
                        help_text.append(lines[i].strip())
                    config.help_text = '\n'.join(help_text)
            
            config.parent = current_menu
            current_menu.children.append(config)
            
        i += 1
    
    return root

class MenuConfig:
    def __init__(self, stdscr):
        self.stdscr = stdscr
        self.config_file = os.path.join(os.path.dirname(__file__), 'Config.in')
        self.config = parse_config(self.config_file)
        self.current_menu = self.config
        self.selected_idx = 0
        self.offset = 0
        self.visible_entries = []
        self.update_visible_entries()
        
    def update_visible_entries(self):
        """Update the list of currently visible menu entries."""
        self.visible_entries = [
            entry for entry in self.current_menu.children 
            if entry.visible
        ]
    
    def draw(self):
        """Draw the menu interface."""
        self.stdscr.clear()
        height, width = self.stdscr.getmaxyx()
        
        # Draw header
        header = f"IR0 Kernel Configuration - {self.current_menu.prompt}"
        self.stdscr.addstr(0, (width - len(header)) // 2, header, curses.A_BOLD)
        
        # Draw menu items
        max_items = height - 4
        end_idx = min(self.offset + max_items, len(self.visible_entries))
        
        for i in range(self.offset, end_idx):
            entry = self.visible_entries[i]
            y = i - self.offset + 2
            
            # Highlight selected item
            attr = curses.A_REVERSE if i == self.selected_idx else curses.A_NORMAL
            
            # Draw menu item
            if entry.type == "menu":
                text = f"  {entry.prompt} --->"
                self.stdscr.addstr(y, 2, text, attr)
            elif entry.type == "config":
                value = "[*]" if entry.value else "[ ]"
                text = f"{value} {entry.prompt}"
                self.stdscr.addstr(y, 2, text, attr)
        
        # Draw footer
        footer = "↑/↓: Navigate  Enter: Select  Space: Toggle  Q: Quit  S: Save  L: Load"
        self.stdscr.addstr(height-2, 2, footer, curses.A_DIM)
        
        # Show help text if available
        if self.visible_entries and 0 <= self.selected_idx < len(self.visible_entries):
            entry = self.visible_entries[self.selected_idx]
            if entry.help_text:
                help_lines = entry.help_text.split('\n')
                for i, line in enumerate(help_lines[:height-4]):
                    self.stdscr.addstr(height-4-i, 2, line, curses.A_DIM)
        
        self.stdscr.refresh()
    
    def run(self):
        """Run the main menu loop."""
        while True:
            self.draw()
            key = self.stdscr.getch()
            
            if key == ord('q') or key == 27:  # Q or ESC
                if self.current_menu != self.config:
                    self.current_menu = self.current_menu.parent
                    self.selected_idx = 0
                    self.offset = 0
                    self.update_visible_entries()
                else:
                    break
                    
            elif key == curses.KEY_UP and self.selected_idx > 0:
                self.selected_idx -= 1
                if self.selected_idx < self.offset:
                    self.offset = self.selected_idx
                    
            elif key == curses.KEY_DOWN and self.selected_idx < len(self.visible_entries) - 1:
                self.selected_idx += 1
                if self.selected_idx >= self.offset + (self.stdscr.getmaxyx()[0] - 4):
                    self.offset += 1
                    
            elif key == ord(' ') and self.visible_entries:
                entry = self.visible_entries[self.selected_idx]
                if entry.type == "config":
                    entry.value = not entry.value
                    
            elif key == ord('\n') and self.visible_entries:  # Enter
                entry = self.visible_entries[self.selected_idx]
                if entry.type == "menu" and entry.children:
                    self.current_menu = entry
                    self.selected_idx = 0
                    self.offset = 0
                    self.update_visible_entries()
                    
            elif key == ord('s'):
                self.save_config()
                
            elif key == ord('l'):
                self.load_config()
                
    def save_config(self):
        """Save the current configuration to .config."""
        config = {}
        
        def collect_configs(menu):
            for entry in menu.children:
                if entry.type == "config":
                    config[entry.name] = entry.value
                collect_configs(entry)
        
        collect_configs(self.config)
        
        with open('.config', 'w') as f:
            for key, value in config.items():
                f.write(f"{key}={1 if value else 0}\n")
    
    def load_config(self):
        if not os.path.exists('.config'):
            return
            
        config = {}
        try:
            with open('.config', 'r') as f:
                for line in f:
                    if '=' in line:  # Asegurarse de que la línea contenga un signo igual
                        key, value = line.strip().split('=', 1)  # Dividir solo en el primer '='
                        config[key.strip()] = int(value.strip()) > 0
        except Exception as e:
            print(f"Error al cargar la configuración: {e}")
            return
        
        def apply_config(menu):
            for entry in menu.children:
                if entry.type == "config" and entry.name in config:
                    entry.value = config[entry.name]
                if hasattr(entry, 'children'):
                    apply_config(entry)
        
        apply_config(self.config)

def main(stdscr):
    # Initialize curses
    curses.curs_set(0)  # Hide cursor
    stdscr.clear()
    
    # Create and run the menu
    try:
        menu = MenuConfig(stdscr)
        menu.run()
    except Exception as e:
        # Restore terminal on error
        curses.endwin()
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        curses.endwin()

if __name__ == "__main__":
    curses.wrapper(main)
