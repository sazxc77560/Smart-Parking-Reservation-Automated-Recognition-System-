# -*- coding: UTF-8 -*-
import cv2
import torch
import copy
import os
import time
import subprocess
import easyocr
import numpy as np
import sys
import re

# 引用 YOLO 相關模組
from models.experimental import attempt_load
from utils.datasets import letterbox
from utils.general import check_img_size, non_max_suppression_plate, scale_coords, xyxy2xywh
from utils.torch_utils import select_device

#########################################################
# 1. 系統初始化與設定
#########################################################
print("[System] 正在初始化 EasyOCR (請稍候)...")
# RPi 務必設定 gpu=False，否則會記憶體不足崩潰
reader = easyocr.Reader(['en'], gpu=False, verbose=False)

weights_path = "weights/best.pt"
device = torch.device("cpu")  # RPi 強制使用 CPU

print(f"[System] 正在載入 YOLO 模型: {weights_path}")
model = attempt_load(weights_path, map_location=device)

# 定義 Pipe 路徑 (必須與 C 語言程式一致)
PIPE_PATH = "/tmp/lpr_pipe"

#########################################################
# 2. 功能函式：Pipe 傳輸
#########################################################
def send_via_pipe(plate_text):
    """將車牌號碼寫入 Pipe，傳送給 C 語言控制程式"""
    print(f"[Pipe] 準備傳送車牌 [{plate_text}] 給 Gate Process...")
    
    try:
        # 1. 檢查 Pipe 是否存在，不存在則建立
        if not os.path.exists(PIPE_PATH):
            os.mkfifo(PIPE_PATH)
            print("[Pipe] 已建立新的 Pipe 檔案")

        # 2. 開啟 Pipe (以寫入模式)
        # 注意：如果 C 語言接收端沒有在跑，這裡會暫停 (Block) 等待
        pipe_fd = os.open(PIPE_PATH, os.O_WRONLY)
        
        # 3. 寫入資料 (必須轉成 bytes)
        # 加上換行符號 \n 讓 C 語言容易讀取
        msg = f"{plate_text}\n"
        os.write(pipe_fd, msg.encode('utf-8'))
        
        # 4. 關閉
        os.close(pipe_fd)
        print("[Pipe] ✅ 傳送成功！")
        return True
        
    except Exception as e:
        print(f"[Pipe Error] 傳送失敗: {e}")
        print("提示：請確認 C 語言接收端 (./gate_control) 是否正在執行？")
        return False

#########################################################
# 3. 功能函式：拍照與辨識
#########################################################
def take_photo(filename="realtime_plate.jpg"):
    """使用系統指令拍照，這是 Bookworm 系統最穩定的方法"""
    print("[Camera] 📷 正在拍照...")
    # -t 500: 暖機 0.5 秒
    # --width 640 --height 480: 配合 YOLO 訓練尺寸，加快速度
    cmd = f"rpicam-jpeg -t 500 -o {filename} --width 640 --height 480"
    
    try:
        # 使用 subprocess 呼叫系統指令
        subprocess.run(cmd, shell=True, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except subprocess.CalledProcessError:
        print("[Camera Error] ❌ 拍照失敗，請檢查相機連接或指令")
        return False

def fix_plate_order(text):
    """
    修正車牌順序錯誤的問題
    例如: 把 '2222AAA' (數字+英文) 修正回 'AAA2222' (英文+數字)
    """
    # Regex 邏輯: 檢查字串是否為「開頭是數字」接著「結尾是英文」
    # ^(\d+)  : 開頭是一串數字 (Group 1)
    # ([A-Z]+)$ : 結尾是一串英文 (Group 2)
    match = re.match(r'^(\d+)([A-Z]+)$', text)
    
    if match:
        numbers = match.group(1) # 抓出 2222
        letters = match.group(2) # 抓出 AAA
        
        # 進行邏輯判斷：通常車牌不會是純數字在前英文在後 (視台灣舊式車牌而定，但這裡假設您的案例)
        # 如果發現這種顛倒狀況，就強制交換
        print(f"[OCR Fix] 偵測到順序顛倒: {text} -> 修正為: {letters}{numbers}")
        return letters + numbers
        
    return text

def ocr_plate_easyocr(plate_img):
    """EasyOCR 辨識核心 (含長度限制與順序修正)"""
    if plate_img is None or plate_img.size == 0:
        return ""
    
    plate_rgb = cv2.cvtColor(plate_img, cv2.COLOR_BGR2RGB)
    
    try:
        # allowlist: 只辨識大寫英文與數字
        result = reader.readtext(plate_rgb, detail=0, allowlist='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')
        
        full_text = "".join(result)
        clean_text = full_text.replace(" ", "")
        
        # 1. 【新增功能】修正順序 (2222AAA -> AAA2222)
        clean_text = fix_plate_order(clean_text)

        # 2. 限制最多 6 個字
        if len(clean_text) > 7:
            clean_text = clean_text[:7]
            
        return clean_text

    except Exception as e:
        print(f"[OCR Error] {e}")
        return ""

def detect_and_recognize(image_path):
    """
    流程：讀取照片 -> YOLO 偵測 -> 裁切 -> EasyOCR 辨識
    回傳：辨識到的車牌字串 (Str) 或 None
    """
    # 1. 讀取照片
    orgimg = cv2.imread(image_path)
    if orgimg is None:
        print("[Error] 無法讀取照片檔案")
        return None

    # 2. 圖片前處理 (Letterbox)
    img_size = 640
    img0 = copy.deepcopy(orgimg)
    h0, w0 = orgimg.shape[:2]
    
    imgsz = check_img_size(img_size, s=model.stride.max())
    img = letterbox(img0, new_shape=imgsz)[0]
    img = img[:, :, ::-1].transpose(2, 0, 1).copy()
    img = torch.from_numpy(img).to(device).float()
    img /= 255.0
    if img.ndimension() == 3:
        img = img.unsqueeze(0)

    # 3. YOLO 推理
    pred = model(img)[0]
    pred = non_max_suppression_plate(pred, 0.3, 0.5) # conf_thres, iou_thres

    found_plate_text = None

    for det in pred:
        if len(det):
            # 還原座標
            det[:, :4] = scale_coords(img.shape[2:], det[:, :4], orgimg.shape).round()
            
            for *xyxy, conf, _, cls in det:
                # 取得座標
                x1, y1, x2, y2 = int(xyxy[0]), int(xyxy[1]), int(xyxy[2]), int(xyxy[3])
                
                # 邊界保護
                h, w = orgimg.shape[:2]
                x1, y1 = max(0, x1), max(0, y1)
                x2, y2 = min(w, x2), min(h, y2)
                
                # 裁切車牌
                plate_crop = orgimg[y1:y2, x1:x2]
                
                # 4. 呼叫 OCR
                print(f"[System] 🔍 偵測到車牌區域 (信心度: {conf:.2f})，正在辨識...")
                
                # 存小圖 debug
                cv2.imwrite("debug_crop.jpg", plate_crop)
                
                plate_text = ocr_plate_easyocr(plate_crop)
                
                if len(plate_text) > 1: # 過濾太短的誤判
                    found_plate_text = plate_text
                    
                    # 畫框並存檔結果
                    cv2.rectangle(orgimg, (x1, y1), (x2, y2), (0, 255, 0), 2)
                    cv2.putText(orgimg, plate_text, (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)
                    
                    break # 取信心度最高的一個即可
        
        if found_plate_text: break

    if not found_plate_text:
        print("[System] ❌ 畫面中未偵測到車牌")
        return None
    
    # 儲存最終結果圖
    cv2.imwrite("final_result.jpg", orgimg)
    return found_plate_text

#########################################################
# 主程式迴圈 (Main Loop)
#########################################################
if __name__ == "__main__":
    print("\n" + "="*50)
    print("🚗 車牌辨識系統已啟動 (RPi Client)")
    print("📝 按 Enter 拍照 -> 辨識 -> 人工驗證 -> 傳送訊號(Pipe)")
    print("="*50)

    while True:
        try:
            print("\n>>> 等待指令...")
            input(">>> 請按 [Enter] 鍵開始拍照 (或按 Ctrl+C 離開): ")
            
            # 1. 拍照
            if take_photo("realtime_plate.jpg"):
                
                # 2. 辨識
                final_plate = detect_and_recognize("realtime_plate.jpg")
                
                # 3. 人工驗證流程
                if final_plate:
                    while True:
                        print(f"\n📢 系統辨識結果: [ {final_plate} ]")
                        print("-" * 30)
                        user_input = input("❓ 結果正確嗎？ (y: 正確 / n: 手動輸入 / r: 重新拍照): ").lower().strip()
                        
                        if user_input == 'y':
                            print(f"✅ 確認車牌: {final_plate}")
                            # 4. 透過 Pipe 傳送給 C 語言程式
                            send_via_pipe(final_plate)
                            break 
                            
                        elif user_input == 'n':
                            manual_input = input("⌨️ 請輸入正確車牌: ").upper().strip()
                            if len(manual_input) > 0:
                                final_plate = manual_input
                                print(f"✅ 已修正車牌: {final_plate}")
                                # 4. 透過 Pipe 傳送給 C 語言程式
                                send_via_pipe(final_plate)
                                break
                            else:
                                print("❌ 輸入空白，請重新操作")
                                
                        elif user_input == 'r':
                            print("🔄 放棄本次結果，重新拍照...")
                            break # 跳出內層迴圈，回到外層 input
                            
                        else:
                            print("⚠️ 無效指令，請輸入 y, n 或 r")
                
                # 如果沒辨識到車牌 (final_plate is None)
                else:
                    retry = input("❌ 辨識失敗，是否重試？ (y/n): ").lower()
                    if retry != 'y':
                        continue

        except KeyboardInterrupt:
            print("\n[System] 程式已結束。再見！")
            break
