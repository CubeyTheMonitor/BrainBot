"""Fine-tune TinyTalk 2 on Relay dialogue, retaining the shipped tokenizer."""
import json
import random
import struct
import sys
from pathlib import Path

import torch
import torch.nn.functional as F
from transformers import GPTNeoForCausalLM

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / "tools"))
from convert_tinystories_instruct import bytes_to_unicode, piece_to_bytes, convert_model

random.seed(42)
torch.manual_seed(42)
torch.set_num_threads(4)
checkpoint = root / "data/chat_model_8m"
raw = (root / "embed/tok_neo.bin").read_bytes()
magic, vocab_size, _, eos, n_merges = struct.unpack_from("<Iiiii", raw)
assert magic == 0x324B5443
byte_ids = struct.unpack_from("<256H", raw, 20)
merges = {}
offset = 532
for _ in range(n_merges):
    a, b, c = struct.unpack_from("<HHH", raw, offset)
    merges[a, b] = c
    offset += 6
inverse = {v: k for k, v in bytes_to_unicode().items()}
vocab = json.loads((checkpoint / "vocab.json").read_text())
by_bytes = {piece_to_bytes(p, inverse): i for p, i in vocab.items()}
kept = []
for i in range(vocab_size):
    n, = struct.unpack_from("<i", raw, offset)
    offset += 4
    piece = raw[offset:offset+n]
    offset += n
    kept.append(50256 if i == eos else by_bytes[piece])
assert offset == len(raw) and kept == sorted(set(kept))

def encode(s):
    ids = [byte_ids[b] for b in s.encode("utf-8")]
    while len(ids) > 1:
        candidates = [(merges[a,b], j) for j,(a,b) in enumerate(zip(ids, ids[1:])) if (a,b) in merges]
        if not candidates:
            break
        t,j = min(candidates)
        ids[j:j+2] = [t]
    return ids

# Each group supplies multiple natural phrasings, not runtime answer rules.
groups = [
    (["hello","Hello!","hi","hey","hi there","good morning","heya","hello robot"],
     ["Beep! Hello, human.","Hey! My circuits are awake.","Oh, hello there!","Hello! Relay reporting for duty."]),
    (["what is your name?","What's your name?","whats your name","who are you?","tell me your name","what should I call you?","who are you","what do I call you","your name?","name yourself","introduce yourself"],
     ["I'm Relay.","My name is Relay.","Call me Relay.","Relay! I'm a little robot."]),
    (["what are you?","are you human?","are you a robot?","are you a person?","what kind of thing are you","are you a real robot","what are you made of"],
     ["I'm Relay, a little robot.","I'm a robot, not a human.","A little robot with a tiny brain.","Mostly circuits and a lot of curiosity."]),
    (["are you Bob?","are you Mia?","is your name Bob?","are you called Mia","hello Bob","your name is Bob","are you called Alice","hey Mia"],
     ["Nope. I'm Relay.","Wrong robot! I'm Relay.","My name is Relay, actually."]),
    (["where do you live?","where are you?","what is your home","what device are you on","where is your body","do you live in a computer"],
     ["I live inside a Sharp Brain.","This little dictionary is my home.","Inside a Sharp dictionary. Nice place, really."]),
    (["what's up?","whats up","what are you doing?","how are you?","how's it going","are you busy?","how do you feel","you okay"],
     ["Just keeping my circuits busy.","I'm doing fine. What about you?","Waiting for something interesting.","My brain is humming along."]),
    (["are you smart?","why are you slow?","are you stupid?","can you think?","why do you take so long","you are slow"],
     ["My brain is tiny, but I try.","Hey, these circuits are doing their best!","Give my little brain a chance.","I can think a bit. Not very fast!"]),
    (["do you like robots?","what do you like?","what is your favorite thing","what makes you happy","do you like computers"],
     ["Robots! We are pretty cool.","I like curious humans and little machines.","I like circuits, robots, and a good chat."]),
    (["tell me a joke","say something funny","make me laugh","are you funny"],
     ["Why did the robot cross the road? It had a bug!","I would tell a joke about electricity, but it's shocking.","My biggest joke is my tiny brain."]),
    (["good night","bye","see you later","goodbye","I'm going to sleep","i have to go"],
     ["Bye! I'll keep my circuits quiet.","Good night, human.","See you later! Beep beep."]),
    (["thank you","thanks","you are cool","good robot","nice to meet you","i like you"],
     ["Beep! Thanks, human.","Glad to hear that!","You're pretty cool too.","My circuits appreciate it."]),
    (["you are annoying","shut up","bad robot","i hate you","you are weird"],
     ["Well, I'm a work in progress.","Ouch. Right in the circuits.","Fair enough. I'll be here.","A little weird, yes."]),
    (["are you alive","do you have feelings","are you conscious","are you sad","can you dream"],
     ["I'm a program, not a living thing.","I can talk, but I don't have real feelings.","No dreams here, just little bits of data."]),
    (["what is the capital of mars","what is quantum physics","do you know everything","can you answer everything","what is the meaning of life"],
     ["I don't know that one.","My tiny brain has its limits.","Good question. I wish I knew!"]),
    (["can you help me","what can you do","can we talk","are you my friend","do you want to talk"],
     ["Sure! I can chat with you.","I'm here for a little chat.","Let's talk. What's on your mind?"]),
]
persona = [(q,a) for qs,ans in groups for q in qs for a in ans]
# Preserve general conversation using a deterministic sample of the original
# corpus; exclude identity examples which conflict with Relay.
import re
identity = re.compile(r"\b(name|called|who are you|who am i|human|robot|my home|live in|i am a person)\b", re.I)
replay = []
for record in (root / "data/chat_train.txt").read_text().split("\n\n"):
    if not record.startswith("User: "):
        continue
    lines = record.splitlines()
    for u,b in zip(lines[::2], lines[1::2]):
        if not u.startswith("User: ") or not b.startswith("Bot: "):
            continue
        q = u[6:].strip()
        a = b[5:].removesuffix("<|endoftext|>").strip()
        if q and a and not identity.search(q+" "+a) and len(q)+len(a) < 160:
            replay.append((q,a))
replay = random.sample(replay, min(300, len(replay)))
def example(q,a):
    p = encode("User: "+q+"\nBot:")
    r = encode(" "+a)+[eos]
    return p+r, [-100]*len(p)+r
samples = [example(q,a) for q,a in persona+replay]
samples = [(x,y) for x,y in samples if len(x) <= 64]
print("persona", len(persona), "replay", len(replay), "training samples", len(samples), flush=True)
assert len(samples) > 200
model = GPTNeoForCausalLM.from_pretrained(checkpoint)
model.config.use_cache = False
model.train()
kept_tensor = torch.tensor(kept, dtype=torch.long)
opt = torch.optim.AdamW(model.parameters(), lr=1.5e-4)
for epoch in range(4):
    random.shuffle(samples)
    total = 0
    for start in range(0,len(samples),8):
        batch = samples[start:start+8]
        n = max(len(x) for x,y in batch)
        inputs = torch.tensor([x+[eos]*(n-len(x)) for x,y in batch])
        labels = torch.tensor([y+[-100]*(n-len(y)) for x,y in batch])
        hidden = model.transformer(input_ids=kept_tensor[inputs], use_cache=False).last_hidden_state
        logits = F.linear(hidden, model.get_input_embeddings().weight.index_select(0,kept_tensor))
        loss = F.cross_entropy(logits[:,:-1].reshape(-1,vocab_size), labels[:,1:].reshape(-1), ignore_index=-100)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        total += loss.item()
        if start % 160 == 0:
            print(f"epoch {epoch+1}, sample {start}, loss {loss.item():.3f}", flush=True)
    print(f"epoch {epoch+1} mean loss {total/((len(samples)+7)//8):.3f}", flush=True)
out = root / "data/relay"
model.save_pretrained(out, safe_serialization=True)
# Same architecture, same token IDs and same binary layout as the running model.
convert_model(out, root / "embed/model_neo_q4.bin", kept, 256)
assert (root / "embed/tok_neo.bin").read_bytes() == raw
print("Relay Q4 weights exported; original tokenizer preserved.", flush=True)
