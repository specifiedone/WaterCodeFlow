import os
import sys
import time
import linecache
import re
import math


# ============================================================
# Trace Scope Configuration (ROBUST)
# ============================================================

# 👇 Change this only if needed
TARGET_ROOT_PATH = "/workspaces/WaterCodeFlow"

# Normalize aggressively (important in containers / symlinks)
TARGET_ROOT_PATH = os.path.realpath(os.path.abspath(TARGET_ROOT_PATH))


# Print once so you can visually confirm correctness
print("\n[TRACE CONFIG]")
print("Target root :", TARGET_ROOT_PATH)
print("This file   :", os.path.realpath(os.path.abspath(__file__)))
print("-" * 60)


# ============================================================
# Tracer State
# ============================================================

step_counter = 0
prev_state = {}
start_wall_time = time.time()

# Track which function we're currently tracing to manage state properly
current_traced_function = None


# ============================================================
# Path Filter
# ============================================================

def should_trace(frame):
    """Only trace code within our target password strength logic"""
    filename = frame.f_code.co_filename
    if not filename:
        return False

    try:
        filename = os.path.realpath(os.path.abspath(filename))
    except Exception:
        return False

    # Must be in our target path
    if not filename.startswith(TARGET_ROOT_PATH):
        return False
    
    # Exclude the tracer function itself to prevent recursion
    func_name = frame.f_code.co_name
    if func_name in ('tracer', 'should_trace'):
        return False
    
    return True


# ============================================================
# Tracer Function
# ============================================================

def tracer(frame, event, arg):
    global step_counter, prev_state, current_traced_function

    if not should_trace(frame):
        return None

    step_counter += 1

    filename = os.path.realpath(frame.f_code.co_filename)
    lineno = frame.f_lineno
    func_name = frame.f_code.co_name
    code_line = linecache.getline(filename, lineno).rstrip()

    print(f"\n{'='*90}")
    print(f"STEP #{step_counter}")
    print(f"EVENT    : {event.upper()}")
    print(f"FUNCTION : {func_name}")
    print(f"LOCATION : {filename}:{lineno}")
    print(f"SOURCE   : {code_line}")
    print(f"{'-'*90}")

    # ---- Locals inspection ----
    current_locals = frame.f_locals.copy()

    if not current_locals:
        print("  [LOCALS] (none)")
    else:
        for name, value in current_locals.items():
            # Skip internal variables
            if name.startswith('__'):
                continue
                
            v_type = type(value).__name__
            v_id = hex(id(value))

            status = " "
            if name not in prev_state:
                status = "🆕 NEW"
            else:
                try:
                    if prev_state[name] != value:
                        status = "Δ CHANGED"
                except Exception:
                    status = "Δ CHANGED"

            # Truncate long values for readability
            repr_val = repr(value)
            if len(repr_val) > 100:
                repr_val = repr_val[:97] + "..."

            print(f"  {status:<10} {name} ({v_type})")
            print(f"             Value : {repr_val}")
            print(f"             Addr  : {v_id}")

    # ---- Event details ----
    if event == "return":
        print(f"\n  <<< RETURN VALUE: {repr(arg)}")

    elif event == "exception":
        exc_type, exc_val, _ = arg
        print(f"\n  !!! EXCEPTION: {exc_type.__name__}: {exc_val}")

    prev_state = current_locals.copy()
    return tracer


# ============================================================
# Target Program - Password Strength Checker
# ============================================================

def check_password_strength(pwd):
    """Check password strength and return score, entropy, and feedback"""
    score = 0
    feedback = []

    # Length check
    if len(pwd) >= 12:
        score += 2
    elif len(pwd) >= 8:
        score += 1
    else:
        feedback.append("Too short")

    # Uppercase check
    if re.search(r"[A-Z]", pwd):
        score += 1
    else:
        feedback.append("No uppercase")

    # Lowercase check
    if re.search(r"[a-z]", pwd):
        score += 1

    # Numbers check
    if re.search(r"[0-9]", pwd):
        score += 1
    else:
        feedback.append("No numbers")

    # Special characters check
    if re.search(r"[!@#$%^&*(),.?\":{}|<>]", pwd):
        score += 1
    else:
        feedback.append("No specials")

    # Shannon entropy calculation
    chars = set(pwd)
    entropy = 0.0
    for char in chars:
        p_x = pwd.count(char) / len(pwd)
        entropy -= p_x * math.log2(p_x)

    # Determine rating
    if score >= 5 and entropy > 3.0:
        rating = "✅ SECURE"
    elif score >= 3:
        rating = "⚠️ WEAK"
    else:
        rating = "❌ DANGEROUS"

    return score, entropy, rating, feedback


# ============================================================
# Main Execution
# ============================================================

# Enable tracing
sys.settrace(tracer)

passwords_to_check = [
    "password123",
    "Admin_2024!",
    "qwerty",
    "S0m3th1ng_Str0ng_H3r3",
    "12345678",
    "Complex!99"
]

print(f"\n{'PASSWORD':<25} | {'ENTROPY':<10} | {'STRENGTH'}")
print("-" * 55)

for pwd in passwords_to_check:
    score, entropy, rating, feedback = check_password_strength(pwd)
    
    print(f"{pwd:<25} | {entropy:<10.2f} | {rating}")
    if feedback:
        print(f"   -> Tips: {', '.join(feedback)}")

print("-" * 55)
print("Audit Complete. All passwords processed.")

# Disable tracing
sys.settrace(None)