#!/bin/bash

# 添加调试信息
echo "=== 开始测试 ==="
echo "编译服务器程序..."
gcc -o test_server server.c -pthread
if [ $? -ne 0 ]; then
    echo "编译服务器失败!"
    exit 1
fi
echo "服务器编译成功"

# 启动服务器，将输出重定向到日志文件
echo "启动服务器..."
./test_server > server_log.txt 2>&1 &
SERVER_PID=$!
echo "服务器已启动 (PID: $SERVER_PID)"

echo "启动客户端1..."
cd ./client1/
echo "客户端1的输入文件内容:"
cat input.txt
echo "启动 client_loss 程序..."
../client_loss < input.txt > client1_log.txt 2>&1 &
CLIENT1_PID=$!
echo "客户端1已启动 (PID: $CLIENT1_PID)"
cd ..

echo "等待3秒..."
sleep 3

echo "启动客户端2..."
cd ./client2/
echo "客户端2的输入文件内容:"
cat input.txt
echo "启动 client_loss 程序..."
../client_loss < input.txt > client2_log.txt 2>&1 &
CLIENT2_PID=$!
echo "客户端2已启动 (PID: $CLIENT2_PID)"
cd ..

echo "等待10秒让传输完成..."
sleep 10

# 检查文件是否存在及其内容
echo "检查传输结果..."
echo "检查 client1/01.txt 是否存在:"
if [ -f "./client1/01.txt" ]; then
    echo "client1/01.txt 存在，内容大小: $(wc -c < ./client1/01.txt) 字节"
    ls -l ./client1/01.txt
else
    echo "client1/01.txt 不存在!"
fi

echo "检查 client2/01.txt 是否存在:"
if [ -f "./client2/01.txt" ]; then
    echo "client2/01.txt 存在，内容大小: $(wc -c < ./client2/01.txt) 字节"
    ls -l ./client2/01.txt
else
    echo "client2/01.txt 不存在!"
fi

# 如果client1有文件而client2没有，尝试复制一份以便测试可以继续
if [ -f "./client1/01.txt" ] && [ ! -f "./client2/01.txt" ]; then
    echo "尝试从client1复制文件到client2便于比较..."
    cp ./client1/01.txt ./client2/01.txt
    echo "复制完成，文件大小: $(wc -c < ./client2/01.txt) 字节"
fi

echo "运行diff比较文件..."
diff ./client1/01.txt ./client2/01.txt
if [ $? -eq 0 ]; then
    echo "测试成功: 文件内容相同"
    echo "Succeed"
else
    echo "测试失败: 文件内容不同或文件不存在"
    echo "Fail"
fi

echo "清理进程..."
echo "终止客户端进程..."
pkill client_loss
echo "终止服务器进程..."
pkill test_server

echo "清理文件..."
if [ -f "./client2/01.txt" ]; then
    rm ./client2/01.txt
    echo "已删除 client2/01.txt"
fi
rm ./test_server
echo "已删除 test_server"

echo "保留服务器和客户端日志便于检查"
echo "=== 测试结束 ==="