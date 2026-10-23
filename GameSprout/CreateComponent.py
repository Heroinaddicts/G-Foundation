#!/usr/bin/env python3
"""交互创建 Module 或 View。运行：python3 CreateComponent.py。"""

from pathlib import Path
import re
import shutil
import sys


# 根据脚本位置定位工程，从其他工作目录运行也不会生成到错误的位置。
ROOT = Path(__file__).resolve().parent

CPP_KEYWORDS = set("""
alignas alignof and and_eq asm auto bitand bitor bool break case catch char
char8_t char16_t char32_t class compl concept const consteval constexpr
constinit const_cast continue co_await co_return co_yield decltype default
delete do double dynamic_cast else enum explicit export extern false float
for friend goto if inline int long mutable namespace new noexcept not not_eq
nullptr operator or or_eq private protected public register reinterpret_cast
requires return short signed sizeof static static_assert static_cast struct
switch template this thread_local throw true try typedef typeid typename union
unsigned using virtual void volatile wchar_t while xor xor_eq
""".split())

# 目录、CMake 保留目标和项目已有的基础目标不能作为新组件名称。
RESERVED_NAMES = {name.casefold() for name in (
    "Interface", "Template", "Build", "CMakeFiles", "all", "clean", "help",
    "install", "test", "edit_cache", "rebuild_cache", "package", "package_source",
    "GameSprout", "GameSproutModuleInterfaces", "GameSproutViewInterfaces",
    "Foundation", "FoundationApi", "FoundationUtils", "FoundationUtilsApi",
    "FoundationUtilsJson", "GRHI", "GRHIApi", "std",
)}


def choose_kind():
    """先选择使用哪一套模板。"""
    print("请选择要创建的类型：\n  1. Module（逻辑模块）\n  2. View（界面模块）")
    choices = {"1": "Module", "module": "Module", "2": "View", "view": "View"}
    while True:
        choice = input("输入 1 或 2：").strip().lower()
        if choice in choices:
            return choices[choice]
        print("选择无效，请输入 1 或 2。")


def validate_name(root, name):
    """名称同时用于目录、C++ 类和 CMake 目标，三者都需要合法。"""
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) or "__" in name:
        raise ValueError("名称必须以英文字母开头，只包含字母、数字或单下划线。")
    if name in CPP_KEYWORDS or name.casefold() in RESERVED_NAMES:
        raise ValueError("这个名称是关键字或工程保留名称，请换一个。")

    # Module 和 View 在同一个编辑器工程中构建，CMake 目标名不能重复。
    # 也检查大小写差异，避免跨 Windows、macOS、Linux 时发生冲突。
    for kind in ("Module", "View"):
        base = root / kind
        if not base.is_dir():
            continue
        if any(child.name.casefold() == name.casefold() for child in base.iterdir()):
            raise FileExistsError(f"{kind} 下已有同名条目 {name}，创建终止，不会覆盖。")

        interfaces = base / "Interface"
        if interfaces.is_dir():
            expected = f"I{name}.h".casefold()
            if any(child.name.casefold() == expected for child in interfaces.iterdir()):
                raise FileExistsError(f"{kind}/Interface 下已有接口 I{name}.h，创建终止，不会覆盖。")


def create_component(root, kind, name):
    """替换模板文件名和正文中的 {name}，把公共接口单独放入 Interface。"""
    validate_name(root, name)
    template_dir = root / "Template" / kind
    if not template_dir.is_dir():
        raise ValueError(f"模板目录不存在：{template_dir}")

    component_dir = root / kind / name
    interface_dir = root / kind / "Interface"
    interface_file = interface_dir / f"I{name}.h"

    # 在创建目标目录前读完全部模板，避免模板缺失或解码失败留下半成品。
    files = {}
    for source in sorted(template_dir.rglob("*")):
        if source.is_symlink():
            raise ValueError(f"模板不支持符号链接：{source}")
        if not source.is_file() or source.name.startswith("."):
            continue

        relative = source.relative_to(template_dir)
        output_name = relative.as_posix().replace("{name}", name)
        content = source.read_bytes()
        if output_name.endswith(".template"):
            output_name = output_name[:-len(".template")]
            # 只替换指定占位符，保留 CMake 的 ${...} 和 C++ 花括号。
            content = content.decode("utf-8").replace("{name}", name).encode("utf-8")

        if output_name in files:
            raise ValueError(f"多个模板生成了同一个文件：{output_name}")
        files[output_name] = content

    for required in ("CMakeLists.txt", f"I{name}.h"):
        if required not in files:
            raise ValueError(f"模板缺少必要文件：{required}")

    interface_content = files.pop(f"I{name}.h")
    component_created = False
    interface_created = False
    try:
        interface_dir.mkdir(parents=True, exist_ok=True)
        component_dir.mkdir()  # 已存在就报错，禁止覆盖。
        component_created = True

        for relative, content in files.items():
            destination = component_dir / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("xb") as output:
                output.write(content)

        # 接口不放在组件私有目录，放到对应类型的公共 Interface 下。
        with interface_file.open("xb") as output:
            interface_created = True
            output.write(interface_content)
    except BaseException:
        # 发生写入失败或用户中断时，只清理本次新建的内容。
        if interface_created:
            interface_file.unlink()
        if component_created:
            shutil.rmtree(component_dir)
        raise

    return component_dir, interface_file


def main():
    kind = choose_kind()
    while True:
        name = input(f"请输入 {kind} 名称（例如 Inventory 或 SettingsView）：").strip()
        try:
            validate_name(ROOT, name)
        except ValueError as error:
            # 只有名称格式错误才重新输入；重名异常直接交给入口报错退出。
            print(f"名称无效：{error}")
            continue
        break

    component_dir, interface_file = create_component(ROOT, kind, name)
    print(f"\n创建成功：{kind} {name}")
    print(f"组件目录：{component_dir}")
    print(f"公共接口：{interface_file}")
    print("重新运行编辑器的 CMake 配置即可发现新组件，也可以单独打开其 CMakeLists.txt。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyboardInterrupt, EOFError):
        print("\n已取消创建。", file=sys.stderr)
        sys.exit(130)
    except (OSError, ValueError) as error:
        print(f"创建失败：{error}", file=sys.stderr)
        sys.exit(1)
