from ultralytics import YOLO
import os
import argparse
import torch

# Dọn bộ nhớ GPU trước khi train
torch.cuda.empty_cache()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', default='dataset/data.yaml', help='path to data.yaml')
    parser.add_argument('--model', default='yolov8n.pt', help='base model name or path')
    parser.add_argument('--epochs', type=int, default=50, help='training epochs')
    parser.add_argument('--device', default='0', help="'0' for GPU, 'cpu' for CPU")
    args = parser.parse_args()

    print("🔥 Starting YOLOv8 Fire Detection Training...")

    data_yaml = os.path.abspath(args.data)
    if not os.path.exists(data_yaml):
        print(f"❌ Dataset not found: {data_yaml}")
        print("Please ensure dataset/data.yaml exists.")
        exit(1)

    print(f"📦 Loading base model: {args.model}")
    model = YOLO(args.model)

    print("🚀 Training...")

    # ✅ Tạo thư mục log nếu chưa có
    os.makedirs("runs/detect/fire_detection", exist_ok=True)

    # ✅ Train model
    results = model.train(
    data=data_yaml,
    epochs=args.epochs,
    imgsz=416,             # ↓ giảm kích thước ảnh (nhanh hơn nhiều, ít giảm độ chính xác)
    batch=8,               # ↑ tăng batch size nếu GPU đủ RAM, giảm nếu lỗi OOM
    device=args.device,
    name='fire_detection_fast',
    project='runs/detect',
    exist_ok=True,
    save=True,
    workers=2,             # ↓ giảm worker cho GPU yếu
    patience=20,           # ↓ dừng sớm khi không cải thiện
    lr0=0.01,
    lrf=0.01,
    momentum=0.937,
    weight_decay=0.0005,
    warmup_epochs=2,       # ↓ warmup ngắn hơn
    box=7.5,
    cls=0.5,
    dfl=1.5,
    hsv_h=0.015,
    hsv_s=0.7,
    hsv_v=0.4,
    translate=0.1,
    scale=0.3,             # ↓ giảm scale để bớt augment
    fliplr=0.5,
    mosaic=0.5,            # ↓ mosaic nhẹ hơn (bớt tốn GPU)
    cache=True,            # ✅ cache ảnh vào RAM (train nhanh hơn rõ rệt)
    close_mosaic=5         # ✅ tắt mosaic ở 5 epoch cuối để ổn định loss
)


    print("\n📊 Evaluating model...")
    metrics = model.val()
    print(f"✅ Training completed! Results in runs/detect/fire_detection/")
    print(f"mAP50: {metrics.box.map50:.3f}, mAP50-95: {metrics.box.map:.3f}")

    # ✅ Xuất model sang ONNX
    try:
        model.export(format='onnx', simplify=True)
    except Exception as e:
        print(f"⚠️ ONNX export failed: {e}")

    print("\n🎉 Done! Best model: runs/detect/fire_detection/weights/best.pt")
    print("📈 TensorBoard logs saved in runs/detect/fire_detection/")
    print("\n👉 To visualize, run:")
    print("   tensorboard --logdir runs/detect")

if __name__ == "__main__":
    main()
