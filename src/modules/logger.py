from PySide6.QtWidgets import QPlainTextEdit
from datetime import datetime
import os
from pathlib import Path

class Logger:
    def __init__(self, log_type:str, log_file_path: str):
        self.log_file_path = log_file_path
        self.log_type = log_type
        # 创建日志文件夹
        os.makedirs(os.path.dirname(self.log_file_path), exist_ok=True)
        date_str = datetime.now().strftime("%Y%m%d")
        filename = f"{self.log_file_path}/{self.log_type}_{date_str}.txt"
        file_path = Path(filename)
        # 可选：清空旧日志
        if not file_path.exists():
            with file_path.open('w', encoding='utf-8') as f:
                f.write("=== Log Start ===\n")

    def log(self, msg_type:str, message: str):
        date_str = datetime.now().strftime("%Y%m%d")
        filename = f"{self.log_file_path}/{self.log_type}_{date_str}.txt"
        file_path = Path(filename)

        if not file_path.exists():
            with file_path.open('w', encoding='utf-8') as f:
                f.write("=== Log Start ===\n")

        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        full_message = f"{timestamp} [{msg_type}] {message}"

        # 写入日志文件
        with open(file_path, "a", encoding="utf-8") as f:
            f.write(full_message + "\n")
            
        return full_message
    
log_dir = os.path.join(os.getcwd(), "Log")        
main_log = Logger("mainLog", log_dir)     
template_log = Logger("template", log_dir)