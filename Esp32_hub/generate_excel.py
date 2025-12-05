import pandas as pd
import os
import serial
import serial.tools.list_ports
import time

# Пути к файлам
project_dir = os.path.dirname(os.path.abspath(__file__))
output_dir = os.path.join(project_dir, 'excel')
output_file = os.path.join(output_dir, 'data_analysis.xlsx')

def get_serial_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("Нет доступных COM портов.")
        return None
    
    print("Доступные COM порты:")
    for i, port in enumerate(ports):
        print(f"{i}: {port.device} - {port.description}")
    
    while True:
        try:
            selection = int(input("Выберите номер порта (например, 0): "))
            if 0 <= selection < len(ports):
                return ports[selection].device
            else:
                print("Неверный номер.")
        except ValueError:
            print("Введите число.")

def read_data_from_esp32(port):
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        time.sleep(2) # Ждем перезагрузки ESP32 при подключении (если есть DTR)
        
        print(f"Подключено к {port}. Отправка команды READ_SD...")
        ser.write(b"READ_SD\n")
        
        data_lines = []
        recording = False
        start_time = time.time()
        
        while True:
            if time.time() - start_time > 10 and not recording:
                print("Таймаут ожидания ответа от ESP32.")
                break
                
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                if line == "---START_FILE---":
                    print("Начало передачи файла...")
                    recording = True
                    continue
                
                if line == "---END_FILE---":
                    print("Конец передачи файла.")
                    break
                
                if recording:
                    print(f"Получено: {line}")
                    data_lines.append(line)
        
        ser.close()
        return data_lines
        
    except Exception as e:
        print(f"Ошибка Serial: {e}")
        return []

def create_excel(data_lines):
    if not data_lines:
        print("Нет данных для записи.")
        return

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    try:
        # Преобразуем список строк в DataFrame
        # Фильтруем пустые строки и нечисловые значения
        valid_data = []
        for line in data_lines:
            try:
                val = float(line)
                valid_data.append(val)
            except ValueError:
                continue
                
        if not valid_data:
            print("Не найдено валидных числовых данных.")
            return

        df = pd.DataFrame(valid_data, columns=['Value'])
        df['Sample'] = df.index + 1
        df = df[['Sample', 'Value']]

        print(f"Создание Excel файла по пути {output_file}...")
        
        writer = pd.ExcelWriter(output_file, engine='xlsxwriter')
        df.to_excel(writer, sheet_name='Data', index=False)
        
        workbook = writer.book
        worksheet = writer.sheets['Data']
        
        chart = workbook.add_chart({'type': 'line'})
        max_row = len(df) + 1 # +1 для заголовка
        
        chart.add_series({
            'name':       'Received Data',
            'categories': ['Data', 1, 0, max_row, 0],
            'values':     ['Data', 1, 1, max_row, 1],
        })
        
        chart.set_x_axis({'name': 'Номер измерения'})
        chart.set_y_axis({'name': 'Значение'})
        chart.set_title({'name': 'График полученных данных'})
        
        worksheet.insert_chart('D2', chart)
        writer.close()
        
        print(f"Успешно создано: {output_file}")

    except Exception as e:
        print(f"Ошибка при создании Excel: {e}")

if __name__ == "__main__":
    port = get_serial_port()
    if port:
        data = read_data_from_esp32(port)
        create_excel(data)
