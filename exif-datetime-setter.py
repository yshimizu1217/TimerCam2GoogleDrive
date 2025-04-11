import os
import datetime
from PIL import Image
from PIL.ExifTags import TAGS
import piexif
import re
import sys

def get_datetime_from_filename(filename):
    """Extract datetime pattern yyyyMMddHHmmss from filename and get datetime"""
    pattern = r'(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})'
    match = re.search(pattern, filename)
    
    if match:
        year, month, day, hour, minute, second = match.groups()
        try:
            dt = datetime.datetime(
                int(year), int(month), int(day), 
                int(hour), int(minute), int(second)
            )
            return dt
        except ValueError:
            return None
    return None

def has_datetime_exif(image_path):
    """Check if image already has DateTimeOriginal EXIF information"""
    try:
        exif_data = piexif.load(image_path)
        # Check if DateTimeOriginal exists
        if "Exif" in exif_data and piexif.ExifIFD.DateTimeOriginal in exif_data["Exif"]:
            return True
        return False
    except:
        return False

def set_exif_datetime(image_path, dt):
    """Set datetime information in EXIF of image file"""
    if dt is None:
        return False
    
    try:
        # Convert datetime format to EXIF format
        exif_datetime = dt.strftime("%Y:%m:%d %H:%M:%S")
        
        # Get existing EXIF information
        exif_dict = {"0th": {}, "Exif": {}, "GPS": {}, "1st": {}, "thumbnail": None}
        try:
            exif_data = piexif.load(image_path)
            if exif_data:
                exif_dict = exif_data
        except:
            pass
        
        # Add datetime to EXIF information
        # Set DateTimeOriginal, DateTimeDigitized, DateTime
        exif_dict["0th"][piexif.ImageIFD.DateTime] = exif_datetime
        exif_dict["Exif"][piexif.ExifIFD.DateTimeOriginal] = exif_datetime
        exif_dict["Exif"][piexif.ExifIFD.DateTimeDigitized] = exif_datetime
        
        # Write EXIF to image
        exif_bytes = piexif.dump(exif_dict)
        piexif.insert(exif_bytes, image_path)
        return True
    except Exception as e:
        print(f"Error setting EXIF for {image_path}: {e}")
        return False

def process_directory(directory, force_update=False):
    """Process JPG files in the specified directory"""
    success_count = 0
    failed_count = 0
    skipped_count = 0
    already_processed_count = 0
    
    jpg_files = [f for f in os.listdir(directory) if f.lower().endswith('.jpg')]
    total_files = len(jpg_files)
    
    print(f"Found {total_files} JPG files in {directory}")
    
    for idx, filename in enumerate(jpg_files):
        file_path = os.path.join(directory, filename)
        
        # Check if already processed (only if force_update is False)
        if not force_update and has_datetime_exif(file_path):
            already_processed_count += 1
            print(f"[{idx+1}/{total_files}] Skipped {filename}: already has EXIF datetime")
            continue
        
        # Get datetime from filename
        dt = get_datetime_from_filename(filename)
        
        if dt:
            # Set EXIF information
            if set_exif_datetime(file_path, dt):
                success_count += 1
                print(f"[{idx+1}/{total_files}] Set EXIF datetime for {filename}: {dt}")
            else:
                failed_count += 1
                print(f"[{idx+1}/{total_files}] Failed to set EXIF for {filename}")
        else:
            skipped_count += 1
            print(f"[{idx+1}/{total_files}] Skipped {filename}: pattern not found")
    
    print(f"\nSummary:")
    print(f"Total files: {total_files}")
    print(f"Success: {success_count}")
    print(f"Failed: {failed_count}")
    print(f"Skipped (no pattern): {skipped_count}")
    print(f"Skipped (already processed): {already_processed_count}")

if __name__ == "__main__":
    # Get directory and options from command line arguments
    import argparse
    
    parser = argparse.ArgumentParser(description='Set EXIF datetime from filename pattern yyyyMMddHHmmss.')
    parser.add_argument('directory', nargs='?', default='.', help='Directory containing the image files (default: current directory)')
    parser.add_argument('--force', '-f', action='store_true', help='Force update EXIF even if already exists')
    parser.add_argument('--recursive', '-r', action='store_true', help='Process subdirectories recursively')
    
    args = parser.parse_args()
    
    if not os.path.isdir(args.directory):
        print(f"Error: {args.directory} is not a valid directory")
        sys.exit(1)
    
    print(f"Processing files in directory: {args.directory}")
    print(f"Force update: {'Yes' if args.force else 'No'}")
    
    if args.recursive:
        # Process recursively
        for root, _, _ in os.walk(args.directory):
            print(f"\nProcessing subdirectory: {root}")
            process_directory(root, args.force)
    else:
        # Process single directory only
        process_directory(args.directory, args.force)
