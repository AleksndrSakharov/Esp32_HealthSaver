#!/usr/bin/env python3
"""
Blood Pressure Data Analyzer
Reads and visualizes blood pressure measurement data from SD card files
"""

import pandas as pd
import matplotlib.pyplot as plt
import sys
import os

def analyze_bp_file(filename):
    """
    Analyze a single blood pressure measurement file
    
    Args:
        filename: Path to the BP measurement file
    """
    try:
        # Read the CSV file (skip the first line which is the title)
        data = pd.read_csv(filename, skiprows=1)
        
        # Extract metadata from the end of the file
        with open(filename, 'r') as f:
            lines = f.readlines()
            for line in lines[-3:]:
                if 'Peak Pressure' in line:
                    peak = line.split(':')[1].strip()
                if 'Total Duration' in line:
                    duration = line.split(':')[1].strip()
        
        print(f"\n{'='*60}")
        print(f"File: {os.path.basename(filename)}")
        print(f"{'='*60}")
        print(f"Peak Pressure: {peak}")
        print(f"Total Duration: {duration}")
        print(f"Number of samples: {len(data)}")
        print(f"Average Pressure: {data['Pressure(ADC)'].mean():.2f}")
        print(f"Pressure Range: {data['Pressure(ADC)'].min()} - {data['Pressure(ADC)'].max()}")
        print(f"{'='*60}\n")
        
        return data
        
    except Exception as e:
        print(f"Error reading file: {e}")
        return None

def plot_bp_data(data, filename):
    """
    Create a visualization of the blood pressure data
    
    Args:
        data: Pandas DataFrame with measurement data
        filename: Original filename for plot title
    """
    if data is None:
        return
    
    plt.figure(figsize=(12, 6))
    
    # Plot pressure over time
    plt.plot(data['Time(ms)'] / 1000, data['Pressure(ADC)'], 'b-', linewidth=2)
    plt.xlabel('Time (seconds)', fontsize=12)
    plt.ylabel('Pressure (ADC Units)', fontsize=12)
    plt.title(f'Blood Pressure Measurement - {os.path.basename(filename)}', fontsize=14, fontweight='bold')
    plt.grid(True, alpha=0.3)
    
    # Add peak marker
    peak_idx = data['Pressure(ADC)'].idxmax()
    plt.plot(data.loc[peak_idx, 'Time(ms)'] / 1000, 
             data.loc[peak_idx, 'Pressure(ADC)'], 
             'ro', markersize=10, label=f"Peak: {data.loc[peak_idx, 'Pressure(ADC)']} ADC")
    
    plt.legend(fontsize=10)
    plt.tight_layout()
    
    # Save the plot
    output_name = filename.replace('.txt', '_plot.png')
    plt.savefig(output_name, dpi=150)
    print(f"Plot saved to: {output_name}")
    
    # Show the plot
    plt.show()

def main():
    """Main function"""
    if len(sys.argv) < 2:
        print("Usage: python analyze_data.py <bp_data_file.txt>")
        print("\nExample: python analyze_data.py bp_12345.txt")
        sys.exit(1)
    
    filename = sys.argv[1]
    
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found!")
        sys.exit(1)
    
    # Analyze the data
    data = analyze_bp_file(filename)
    
    # Plot the data
    if data is not None:
        plot_bp_data(data, filename)

if __name__ == "__main__":
    main()

