@echo off
setlocal

cmake -S . -B build
cmake --build build --config Release

build\Release\ijccrlcli.exe config\example.runner.json
