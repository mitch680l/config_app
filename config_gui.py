import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import threading
import time
from datetime import datetime

class ConfigGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Embedded Project Configuration GUI")
        self.root.geometry("800x600")
        
        # Serial connection variables
        self.serial_connection = None
        self.is_connected = False
        self.receive_thread = None
        
        # COM port settings
        self.com_port = "COM9"  # Default COM port
        self.baud_rate = 115200
        
        self.setup_ui()
        
    def setup_ui(self):
        # Create main frame
        main_frame = ttk.Frame(self.root)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Top toolbar
        self.create_toolbar(main_frame)
        
        # Terminal area
        self.create_terminal(main_frame)
        
    def create_toolbar(self, parent):
        toolbar_frame = ttk.Frame(parent)
        toolbar_frame.pack(fill=tk.X, pady=(0, 5))
        
        # Tools label
        tools_label = ttk.Label(toolbar_frame, text="Tools:", font=("Arial", 10, "bold"))
        tools_label.pack(side=tk.LEFT, padx=(0, 10))
        
        # Connect button
        self.connect_btn = ttk.Button(toolbar_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.pack(side=tk.LEFT, padx=5)
        
        # COM port selection
        ttk.Label(toolbar_frame, text="COM Port:").pack(side=tk.LEFT, padx=(20, 5))
        self.com_var = tk.StringVar(value=self.com_port)
        com_combo = ttk.Combobox(toolbar_frame, textvariable=self.com_var, 
                                 values=["COM8", "COM9", "COM10", "COM11"], 
                                 width=8, state="readonly")
        com_combo.pack(side=tk.LEFT, padx=5)
        com_combo.bind("<<ComboboxSelected>>", self.on_com_port_change)
        
        # Baud rate selection
        ttk.Label(toolbar_frame, text="Baud:").pack(side=tk.LEFT, padx=(20, 5))
        self.baud_var = tk.StringVar(value=str(self.baud_rate))
        baud_combo = ttk.Combobox(toolbar_frame, textvariable=self.baud_var,
                                  values=["9600", "19200", "38400", "57600", "115200"], 
                                  width=8, state="readonly")
        baud_combo.pack(side=tk.LEFT, padx=5)
        baud_combo.bind("<<ComboboxSelected>>", self.on_baud_change)
        
        # Status indicator
        self.status_label = ttk.Label(toolbar_frame, text="Disconnected", foreground="red")
        self.status_label.pack(side=tk.RIGHT, padx=10)
        
    def create_terminal(self, parent):
        terminal_frame = ttk.LabelFrame(parent, text="Serial Terminal")
        terminal_frame.pack(fill=tk.BOTH, expand=True)
        
        # Terminal text area
        self.terminal = scrolledtext.ScrolledText(terminal_frame, height=20, width=80)
        self.terminal.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Input frame
        input_frame = ttk.Frame(terminal_frame)
        input_frame.pack(fill=tk.X, padx=5, pady=(0, 5))
        
        # Command input
        self.command_var = tk.StringVar()
        self.command_entry = ttk.Entry(input_frame, textvariable=self.command_var)
        self.command_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 5))
        self.command_entry.bind("<Return>", self.send_command)
        
        # Send button
        send_btn = ttk.Button(input_frame, text="Send", command=self.send_command)
        send_btn.pack(side=tk.RIGHT)
        
    def on_com_port_change(self, event=None):
        self.com_port = self.com_var.get()
        
    def on_baud_change(self, event=None):
        try:
            self.baud_rate = int(self.baud_var.get())
        except ValueError:
            pass
            
    def toggle_connection(self):
        if not self.is_connected:
            self.connect()
        else:
            self.disconnect()
            
    def connect(self):
        try:
            self.serial_connection = serial.Serial(
                port=self.com_port,
                baudrate=self.baud_rate,
                timeout=1
            )
            self.is_connected = True
            self.connect_btn.config(text="Disconnect")
            self.status_label.config(text="Connected", foreground="green")
            
            # Start receive thread
            self.receive_thread = threading.Thread(target=self.receive_data, daemon=True)
            self.receive_thread.start()
            
            self.log_message(f"Connected to {self.com_port} at {self.baud_rate} baud")
            
        except serial.SerialException as e:
            messagebox.showerror("Connection Error", f"Failed to connect to {self.com_port}: {str(e)}")
            
    def disconnect(self):
        if self.serial_connection:
            self.serial_connection.close()
            self.serial_connection = None
        self.is_connected = False
        self.connect_btn.config(text="Connect")
        self.status_label.config(text="Disconnected", foreground="red")
        self.log_message("Disconnected")
        
    def receive_data(self):
        while self.is_connected and self.serial_connection:
            try:
                if self.serial_connection.in_waiting:
                    data = self.serial_connection.readline().decode('utf-8', errors='ignore')
                    if data.strip():
                        self.root.after(0, self.log_message, data.rstrip(), prefix="")
            except (serial.SerialException, UnicodeDecodeError):
                break
                
    def send_command(self, event=None):
        if not self.is_connected:
            messagebox.showwarning("Not Connected", "Please connect to the device first.")
            return
            
        command = self.command_var.get().strip()
        if command:
            try:
                # Add newline to command if not present
                if not command.endswith('\n'):
                    command += '\n'
                    
                self.serial_connection.write(command.encode('utf-8'))
                self.log_message(f"Sent: {command.rstrip()}", prefix="> ")
                self.command_var.set("")  # Clear input
                
            except serial.SerialException as e:
                messagebox.showerror("Send Error", f"Failed to send command: {str(e)}")
                
    def log_message(self, message, prefix="[INFO] "):
        timestamp = datetime.now().strftime("%H:%M:%S")
        formatted_message = f"{timestamp} {prefix}{message}\n"
        
        self.terminal.insert(tk.END, formatted_message)
        self.terminal.see(tk.END)
        
    def on_closing(self):
        if self.is_connected:
            self.disconnect()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = ConfigGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main() 