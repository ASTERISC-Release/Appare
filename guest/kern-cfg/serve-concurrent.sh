#!/bin/bash

# —— 实例 A → GPU0, 127.0.0.1:11432 —— 
(
  export CUDA_VISIBLE_DEVICES=0
  export GPU_DEVICE_ORDINAL=0
  export OLLAMA_HOST="http://127.0.0.1:11432"
  ollama serve
) &

# —— 实例 B → GPU1, 127.0.0.1:11433 —— 
(
  export CUDA_VISIBLE_DEVICES=1
  export GPU_DEVICE_ORDINAL=1
  export OLLAMA_HOST="http://127.0.0.1:11433"
  ollama serve
) &
