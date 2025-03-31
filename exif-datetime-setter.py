import os
import datetime
from PIL import Image
from PIL.ExifTags import TAGS
import piexif
import re
import sys

def get_datetime_from_filename(filename):
    """ファイル名からyyyyMMddHHmmssのパターンを抽出して日時を取得"""
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
    """画像が既にDateTimeOriginalのEXIF情報を持っているかチェック"""
    try:
        exif_data = piexif.load(image_path)
        # DateTimeOriginalが存在するかチェック
        if "Exif" in exif_data and piexif.ExifIFD.DateTimeOriginal in exif_data["Exif"]:
            return True
        return False
    except:
        return False

def set_exif_datetime(image_path, dt):
    """画像ファイルのEXIFに日時情報を設定"""
    if dt is None:
        return False
    
    try:
        # 日時フォーマットをEXIF形式に変換
        exif_datetime = dt.strftime("%Y:%m:%d %H:%M:%S")
        
        # 既存のEXIF情報を取得
        exif_dict = {"0th": {}, "Exif": {}, "GPS": {}, "1st": {}, "thumbnail": None}
        try:
            exif_data = piexif.load(image_path)
            if exif_data:
                exif_dict = exif_data
        except:
            pass
        
        # EXIF情報に日時を追加
        # DateTimeOriginal, DateTimeDigitized, DateTime を設定
        exif_dict["0th"][piexif.ImageIFD.DateTime] = exif_datetime
        exif_dict["Exif"][piexif.ExifIFD.DateTimeOriginal] = exif_datetime
        exif_dict["Exif"][piexif.ExifIFD.DateTimeDigitized] = exif_datetime
        
        # EXIFを画像に書き込み
        exif_bytes = piexif.dump(exif_dict)
        piexif.insert(exif_bytes, image_path)
        return True
    except Exception as e:
        print(f"Error setting EXIF for {image_path}: {e}")
        return False

def process_directory(directory, force_update=False):
    """指定ディレクトリ内のJPGファイルを処理"""
    success_count = 0
    failed_count = 0
    skipped_count = 0
    already_processed_count = 0
    
    jpg_files = [f for f in os.listdir(directory) if f.lower().endswith('.jpg')]
    total_files = len(jpg_files)
    
    print(f"Found {total_files} JPG files in {directory}")
    
    for idx, filename in enumerate(jpg_files):
        file_path = os.path.join(directory, filename)
        
        # 既に処理済みかチェック（force_updateがFalseの場合のみ）
        if not force_update and has_datetime_exif(file_path):
            already_processed_count += 1
            print(f"[{idx+1}/{total_files}] Skipped {filename}: already has EXIF datetime")
            continue
        
        # ファイル名から日時を取得
        dt = get_datetime_from_filename(filename)
        
        if dt:
            # EXIF情報を設定
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
    # コマンドライン引数からディレクトリとオプションを取得
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
        # 再帰的に処理
        for root, _, _ in os.walk(args.directory):
            print(f"\nProcessing subdirectory: {root}")
            process_directory(root, args.force)
    else:
        # 単一ディレクトリのみ処理
        process_directory(args.directory, args.force)
