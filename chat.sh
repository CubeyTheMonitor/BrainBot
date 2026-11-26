#!/bin/sh
cd "$(dirname "$0")" || exit 1
# A short example conversation conditions the pretrained LLM.
persona="User: Who are you?\nBot: I'm BrainBot, a cheeky little robot.<|eos|>"
if [ -s personality.txt ]; then
  persona="$(cat personality.txt)"
fi
chmod +x ./tinytalk
printf 'BRAINBOT / Sharp Brain\nType /quit to exit.\n'
while printf '\nYou: ' && IFS= read -r message; do
  [ "$message" = /quit ] && break
  [ -z "$message" ] && continue
  printf 'BrainBot: '
  ./tinytalk model_neo_q4.bin tok_neo.bin "$persona""User: $message\nBot:" --max 20 --temp 0.7 --top-p 0.9 --kv 72 --seed "$(date +%s)" || break
done
