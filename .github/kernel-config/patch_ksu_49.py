"""给 SukiSU Ultra v4.0.0 打 Linux 4.9 兼容补丁（幂等，可重复运行）。

v4.0.0 已经比 main 兼容得多，只剩三类缺口：

1. MODULE_IMPORT_NS —— 5.4 才引入
2. TWA_RESUME / TWA_SIGNAL —— task_work_add 的标志位，5.10 才有
   （4.9 是 task_work_add(task, work, bool notify)，notify=true 等价 TWA_RESUME）
3. struct file_operations 的成员：
      remap_file_range     4.20（4.9 叫 clone_file_range，签名也不同）
      fadvise              4.15
      mmap_supported_flags 5.9
      fop_flags            6.12
   这些成员在 4.9 上不存在，直接引用会编译失败，必须用版本守卫包起来。

前两类通过"强制包含一个兼容头"一次覆盖所有目标文件；
第三类只能逐个文件加守卫（目前只有 file_wrapper.c）。

用法:
    python3 patch_ksu_49.py <drivers/kernelsu 目录>
"""
import os
import sys

COMPAT_HEADER = """/* 由 CI 的 patch_ksu_49.py 生成：Linux 4.9 兼容层 */
#ifndef __KSU_49_COMPAT_H
#define __KSU_49_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#ifndef MODULE_IMPORT_NS
#define MODULE_IMPORT_NS(ns)
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
/* 4.9 的 task_work_add 第三个参数是 bool notify，
   notify=true 就是"返回用户态时执行"，等价于后来的 TWA_RESUME */
#ifndef TWA_RESUME
#define TWA_RESUME true
#endif
#ifndef TWA_SIGNAL
#define TWA_SIGNAL false
#endif
#endif

#endif /* __KSU_49_COMPAT_H */
"""

# 4.9 里不存在的内核头文件 -> shim（内容为正确的老位置）
# 依据：把 v4.0.0 用到的全部 #include <linux/...> 跟官方 EMUI9.1 源码包的
#       include/linux + include/uapi/linux 对了一遍。
#       linux/ioctl.h、linux/version.h 是假阳性（前者在 uapi、后者构建时生成）。
SHIMS = {
    "linux/sched/signal.h":     "#include <linux/sched.h>\n",
    "linux/sched/task.h":       "#include <linux/sched.h>\n",
    "linux/sched/task_stack.h": "#include <linux/sched.h>\n",
    "linux/sched/mm.h":         "#include <linux/sched.h>\n",
    "linux/sched/cputime.h":    "#include <linux/sched.h>\n",
    "linux/sched/debug.h":      "#include <linux/sched.h>\n",
    # 4.12 才把 set_memory_* 从 asm/cacheflush.h 挪到 asm/set_memory.h
    "linux/set_memory.h":       "#include <asm/cacheflush.h>\n",
    # 4.19 才从 compiler.h 拆出 compiler_types.h
    "linux/compiler_types.h":   "#include <linux/compiler.h>\n",
}

APPENDS = [
    ("Makefile",
     "ccflags-y += -include $(srctree)/drivers/kernelsu/ksu_49_compat.h",
     "Makefile: 强制包含兼容头"),
    ("Makefile",
     "ccflags-y += -I$(srctree)/drivers/kernelsu/49shim",
     "Makefile: 加入 shim 头目录"),
]

# (文件, 原串, 新串, 说明, 幂等标记)
REPLACES = [
    ("file_wrapper.c",
     "static loff_t ksu_wrapper_remap_file_range(struct file *file_in, loff_t pos_in,",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\n"
     "static loff_t ksu_wrapper_remap_file_range(struct file *file_in, loff_t pos_in,",
     "file_wrapper.c: remap_file_range 函数定义加 4.20 守卫",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\nstatic loff_t ksu_wrapper_remap_file_range"),

    ("file_wrapper.c",
     "\treturn -EINVAL;\n}\n\nstatic int ksu_wrapper_fadvise(",
     "\treturn -EINVAL;\n}\n#endif /* >= 4.20 */\n\n"
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)\n"
     "static int ksu_wrapper_fadvise(",
     "file_wrapper.c: 闭合 remap 守卫、打开 fadvise 守卫",
     "#endif /* >= 4.20 */"),

    ("file_wrapper.c",
     "\treturn -EINVAL;\n}\n\nstatic int ksu_wrapper_release(",
     "\treturn -EINVAL;\n}\n#endif /* >= 4.15 */\n\nstatic int ksu_wrapper_release(",
     "file_wrapper.c: 闭合 fadvise 守卫",
     "#endif /* >= 4.15 */"),

    ("file_wrapper.c",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)\n"
     "\tp->ops.fop_flags = fp->f_op->fop_flags;\n"
     "#else\n"
     "\tp->ops.mmap_supported_flags = fp->f_op->mmap_supported_flags;\n"
     "#endif",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)\n"
     "\tp->ops.fop_flags = fp->f_op->fop_flags;\n"
     "#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)\n"
     "\tp->ops.mmap_supported_flags = fp->f_op->mmap_supported_flags;\n"
     "#endif",
     "file_wrapper.c: mmap_supported_flags 改成 elif >= 5.9",
     "#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)"),

    ("file_wrapper.c",
     "\tp->ops.remap_file_range = fp->f_op->remap_file_range ? ksu_wrapper_remap_file_range : NULL;\n"
     "\tp->ops.fadvise = fp->f_op->fadvise ? ksu_wrapper_fadvise : NULL;",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)\n"
     "\tp->ops.remap_file_range = fp->f_op->remap_file_range ? ksu_wrapper_remap_file_range : NULL;\n"
     "#endif\n"
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)\n"
     "\tp->ops.fadvise = fp->f_op->fadvise ? ksu_wrapper_fadvise : NULL;\n"
     "#endif",
     "file_wrapper.c: remap/fadvise 赋值加守卫",
     "#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)\n\tp->ops.fadvise"),
]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    root = sys.argv[1]
    if not os.path.isdir(root):
        print(f"!! 目录不存在: {root}")
        return 1

    # 1. 兼容头
    hp = os.path.join(root, "ksu_49_compat.h")
    if os.path.exists(hp):
        print("  已存在  ksu_49_compat.h")
    else:
        with open(hp, "w", encoding="utf-8", newline="\n") as f:
            f.write(COMPAT_HEADER)
        print("  已创建  ksu_49_compat.h")

    # 1b. shim 头文件
    made = 0
    for rel, content in SHIMS.items():
        sp = os.path.join(root, "49shim", rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(sp), exist_ok=True)
        if not os.path.exists(sp):
            with open(sp, "w", encoding="utf-8", newline="\n") as f:
                f.write(content)
            made += 1
    print(f"  shim 头文件 {len(SHIMS)} 个（新建 {made}）")

    # 2. 追加强制包含
    for fname, line, desc in APPENDS:
        path = os.path.join(root, fname)
        if not os.path.isfile(path):
            print(f"  跳过 {fname}（不存在）")
            continue
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            src = f.read()
        if line in src:
            print(f"  已打过  {desc}")
            continue
        with open(path, "a", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
            f.write("\n# ---- 4.9 compat (patch_ksu_49.py) ----\n" + line + "\n")
        print(f"  已应用  {desc}")

    # 3. 定点替换
    n = 0
    for fname, old, new, desc, marker in REPLACES:
        path = os.path.join(root, fname)
        if not os.path.isfile(path):
            print(f"  跳过 {fname}（不存在）")
            continue
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            src = f.read()
        if marker in src:
            print(f"  已打过  {desc}")
            continue
        if old not in src:
            print(f"  !! 锚点没找到: {desc}")
            continue
        src = src.replace(old, new, 1)
        with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
            f.write(src)
        print(f"  已应用  {desc}")
        n += 1

    print(f"\n定点替换 {n} 处")
    return 0


if __name__ == "__main__":
    sys.exit(main())
