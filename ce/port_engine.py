"""Port pinned upstream scalar inference to single-core WinCE; no ESP/FreeRTOS dependencies."""
import sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
def replace(a,b):
    global s
    assert a in s, a
    s=s.replace(a,b,1)
replace("#include <esp_heap_caps.h>\n#include <freertos/FreeRTOS.h>\n#include <freertos/task.h>\n#include <freertos/semphr.h>", "#include <stdint.h>")
replace("return heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);","return malloc(n);")
replace("return heap_caps_aligned_alloc(16, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);","void* p=malloc(n+15); return p ? (void*)(((uintptr_t)p+15)&~(uintptr_t)15) : NULL;")
a=s.index("static SemaphoreHandle_t mm_go")
b=s.index("// v3 activation-quantization scratch",a)
s=s[:a]+s[b:]
replace("  ensure_worker();\n","")
a=s.index("  int split = d / 2;")
b=s.index("\n}",a)
s=s[:a]+"""  MatmulArgs self = { xout, x, xq, xs, w, n, row_bytes, 0, d };
  matmul_rows(&self);"""+s[b:]
a=s.index("// ============================================================================")
s=s[:a]+"""static long relay_round(float x) {
  double f=floor((double)x), r=(double)x-f;
  long n=(long)f;
  return r>0.5 || (r==0.5 && (n&1)) ? n+1 : n;
}
"""+s[a:]
for old,new in {"lrintf(":"relay_round(","fabsf(":"fabs(","sqrtf(":"sqrt(","tanhf(":"tanh(","expf(":"exp(","powf(":"pow(","cosf(":"cos(","sinf(":"sin("}.items():
    s=s.replace(old,new)
assert "xSemaphore" not in s and "heap_caps" not in s
Path(sys.argv[2]).write_text(s)
