#!/bin/bash
# YOLOv12-TensorRT环境管理脚本
PROJECT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
VENV_DIR="$PROJECT_DIR/.venv"
# 检查命令行参数
if [ "$1" == "deactivate" ]; then
    # 检查当前是否在虚拟环境中
    if [ -z "$VIRTUAL_ENV" ]; then
        echo "错误: 虚拟环境尚未激活"
        exit 1
    fi
    
    echo "正在退出YOLOv12-TensorRT环境..."
    deactivate
    echo "已退出环境"
    exit 0
fi
# 检查虚拟环境是否存在
if [ ! -d "$VENV_DIR" ]; then
    echo "错误: 虚拟环境未找到。请先创建虚拟环境:"
    echo "  python3 -m venv .venv"
    exit 1
fi
# 激活虚拟环境
echo "正在激活YOLOv12-TensorRT环境..."
source "$VENV_DIR/bin/activate"
# 验证环境
if [ -n "$VIRTUAL_ENV" ]; then
    echo "验证成功: 虚拟环境已激活"
    echo "Python路径: $(which python)"
    echo "Python版本: $(python --version)"
    echo "环境目录: $VIRTUAL_ENV"
    
    # 检查TensorRT是否可用
    if python -c "import tensorrt" &>/dev/null; then
        TENSORRT_VERSION=$(python -c "import tensorrt as trt; print(trt.__version__)")
        echo "TensorRT版本: $TENSORRT_VERSION"
    else
        echo "警告: TensorRT在Python中不可用。可能需要配置符号链接。"
    fi
    
    echo "虚拟环境准备就绪!"
else
    echo "错误: 虚拟环境激活失败"
    exit 1
fi
# 添加一些有用的提示
echo ""
echo "使用提示:"
echo "  - 退出环境: source yolov12_env.sh deactivate"
echo "  - 安装包: pip install <package_name>"
echo "  - 当前目录: $PROJECT_DIR"
echo ""
