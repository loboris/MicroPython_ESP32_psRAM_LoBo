import sys, gc
class Editor:
    KEYMAP = { 
    "\x1b[A" : 0x0b,
    "\x1b[B" : 0x0d,
    "\x1b[D" : 0x1f,
    "\x1b[C" : 0x1e,
    "\x1b[H" : 0x10, 
    "\x1bOH" : 0x10, 
    "\x1b[1~": 0x10, 
    "\x1b[F" : 0x03, 
    "\x1bOF" : 0x03, 
    "\x1b[4~": 0x03, 
    "\x1b[5~": 0xfff1,
    "\x1b[6~": 0xfff2,
    "\x03" : 0x04, 
    "\r" : 0x0a,
    "\x7f" : 0x08, 
    "\x1b[3~": 0x7f,
    "\x1b[Z" : 0x15, 
    "\x19" : 0x18, 
    "\x08" : 0x12, 
    "\x12" : 0x12, 
    "\x11" : 0x11, 
    "\n" : 0x0a,
    "\x13" : 0x13, 
    "\x06" : 0x06, 
    "\x0e" : 0x0e, 
    "\x07" : 0x07, 
    "\x05" : 0x05, 
    "\x1a" : 0x1a, 
    "\x09" : 0x09,
    "\x15" : 0x15, 
    "\x18" : 0x18, 
    "\x16" : 0x16, 
    "\x04" : 0x04, 
    "\x0c" : 0x0c, 
    "\x14" : 0x14, 
    "\x02" : 0x02, 
    "\x01" : 0x01, 
    "\x17" : 0x17, 
    "\x0f" : 0x0f, 
    "\x1b[1;5H": 0x14, 
    "\x1b[1;5F": 0x02, 
    "\x1b[3;5~": 0x18, 
    "\x0b" : 0xfffd,
    "\x1b[M" : 0x1b,
    }

    yank_buffer = []
    find_pattern = ""
    case = "n"
    replc_pattern = ""

    def __init__(self, tab_size, undo_limit):
        self.top_line = self.cur_line = self.row = self.col = self.margin = 0
        self.tab_size = tab_size
        self.changed = ""
        self.message = self.fname = ""
        self.content = [""]
        self.undo = []
        self.undo_limit = max(undo_limit, 0)
        self.undo_zero = 0
        self.autoindent = "y"
        self.mark = None
        self.write_tabs = "n"

    def wr(self, s):
        sys.stdout.write(s)

    def rd_any(self):
        return False

    def rd(self):
        while True:
            try: return sys.stdin.read(1)
            except KeyboardInterrupt: return '\x03'


    def goto(self, row, col):
        self.wr("\x1b[{};{}H".format(row + 1, col + 1))

    def clear_to_eol(self):
        self.wr("\x1b[0K")

    def cursor(self, onoff):
        self.wr("\x1b[?25h" if onoff else "\x1b[?25l")

    def hilite(self, mode):
        if mode == 1: 
            #self.wr("\x1b[1;47m") #white background
            self.wr("\x1b[1m")
        elif mode == 2: 
            self.wr("\x1b[43m")   # yellow
        else: 
            self.wr("\x1b[0m")
    def mouse_reporting(self, onoff):
        self.wr('\x1b[?9h' if onoff else '\x1b[?9l') 

    def scroll_region(self, stop):
        self.wr('\x1b[1;{}r'.format(stop) if stop else '\x1b[r') 

    def scroll_up(self, scrolling):
        Editor.scrbuf[scrolling:] = Editor.scrbuf[:-scrolling]
        Editor.scrbuf[:scrolling] = [''] * scrolling
        self.goto(0, 0)
        self.wr("\x1bM" * scrolling)

    def scroll_down(self, scrolling):
        Editor.scrbuf[:-scrolling] = Editor.scrbuf[scrolling:]
        Editor.scrbuf[-scrolling:] = [''] * scrolling
        self.goto(Editor.height - 1, 0)
        self.wr("\x1bD " * scrolling)

    def get_screen_size(self):
        self.wr('\x1b[999;999H\x1b[6n')
        pos = ''
        char = self.rd() 
        while char != 'R':
            pos += char
            char = self.rd()
        return [int(i, 10) for i in pos.lstrip("\n\x1b[").split(';')]

    def redraw(self, flag):
        self.cursor(False)
        Editor.height, Editor.width = self.get_screen_size()
        Editor.height -= 1
        Editor.scrbuf = [(False,"\x00")] * Editor.height 
        self.row = min(Editor.height - 1, self.row)
        self.scroll_region(Editor.height)
        self.mouse_reporting(True) 
        if sys.implementation.name == "micropython":
            gc.collect()
            if flag: self.message = "{} Bytes Memory available".format(gc.mem_free())

    def get_input(self): 
        while True:
            in_buffer = self.rd()
            if in_buffer == '\x1b': 
                while True:
                    in_buffer += self.rd()
                    c = in_buffer[-1]
                    if c == '~' or (c.isalpha() and c != 'O'):
                        break
            if in_buffer in self.KEYMAP:
                c = self.KEYMAP[in_buffer]
                if c != 0x1b:
                    return c, ""
                else: 
                    mouse_fct = ord((self.rd())) 
                    mouse_x = ord(self.rd()) - 33
                    mouse_y = ord(self.rd()) - 33
                    if mouse_fct == 0x61:
                        return 0x1d, ""
                    elif mouse_fct == 0x60:
                        return 0x1c, ""
                    else:
                        return 0x1b, [mouse_x, mouse_y, mouse_fct] 
            elif ord(in_buffer[0]) >= 32:
                return 0, in_buffer

    def display_window(self): 
        self.cur_line = min(self.total_lines - 1, max(self.cur_line, 0))
        self.col = max(0, min(self.col, len(self.content[self.cur_line])))
        if self.col >= Editor.width + self.margin:
            self.margin = self.col - Editor.width + (Editor.width >> 2)
        elif self.col < self.margin:
            self.margin = max(self.col - (Editor.width >> 2), 0)
        if not (self.top_line <= self.cur_line < self.top_line + Editor.height): 
            self.top_line = max(self.cur_line - self.row, 0)
        self.row = self.cur_line - self.top_line
        self.cursor(False)
        i = self.top_line
        for c in range(Editor.height):
            if i == self.total_lines: 
                if Editor.scrbuf[c] != (False,''):
                    self.goto(c, 0)
                    self.clear_to_eol()
                    Editor.scrbuf[c] = (False,'')
            else:
                l = (self.mark != None and (
                    (self.mark <= i <= self.cur_line) or (self.cur_line <= i <= self.mark)),
                     self.content[i][self.margin:self.margin + Editor.width])
                if l != Editor.scrbuf[c]: 
                    self.goto(c, 0)
                    if l[0]: self.hilite(2)
                    self.wr(l[1])
                    if len(l[1]) < Editor.width:
                        self.clear_to_eol()
                    if l[0]: self.hilite(0)
                    Editor.scrbuf[c] = l
                i += 1
        self.goto(Editor.height, 0)
        self.hilite(1)
        self.wr("{}{} Row: {}/{} Col: {}  {}".format(
            self.changed, self.fname, self.cur_line + 1, self.total_lines,
            self.col + 1, self.message)[:self.width - 1])
        self.clear_to_eol() 
        self.hilite(0)
        self.goto(self.row, self.col - self.margin)
        self.cursor(True)

    def spaces(self, line, pos = None): 
        return (len(line) - len(line.lstrip(" ")) if pos == None else 
                len(line[:pos]) - len(line[:pos].rstrip(" ")))

    def line_range(self):
        return ((self.mark, self.cur_line + 1) if self.mark < self.cur_line else
                (self.cur_line, self.mark + 1))

    def line_edit(self, prompt, default): 
        push_msg = lambda msg: self.wr(msg + "\b" * len(msg)) 
        self.goto(Editor.height, 0)
        self.hilite(1)
        self.wr(prompt)
        self.wr(default)
        self.clear_to_eol()
        res = default
        pos = len(res)
        while True:
            key, char = self.get_input() 
            if key in (0x0a, 0x09): 
                self.hilite(0)
                return res
            elif key == 0x11: 
                self.hilite(0)
                return None
            elif key == 0x1f:
                if pos > 0:
                    self.wr("\b")
                    pos -= 1
            elif key == 0x1e:
                if pos < len(res):
                    self.wr(res[pos])
                    pos += 1
            elif key == 0x10:
                self.wr("\b" * pos)
                pos = 0
            elif key == 0x03:
                self.wr(res[pos:])
                pos = len(res)
            elif key == 0x7f: 
                if pos < len(res):
                    res = res[:pos] + res[pos+1:]
                    push_msg(res[pos:] + ' ') 
            elif key == 0x08: 
                if pos > 0:
                    res = res[:pos-1] + res[pos:]
                    self.wr("\b")
                    pos -= 1
                    push_msg(res[pos:] + ' ') 
            elif key == 0x16: 
                if Editor.yank_buffer:
                    self.wr('\b' * pos + ' ' * len(res) + '\b' * len(res))
                    res = Editor.yank_buffer[0].strip()[:Editor.width - len(prompt) - 2]
                    self.wr(res)
                    pos = len(res)
            elif key == 0: 
                if len(prompt) + len(res) < self.width - 2:
                    res = res[:pos] + char + res[pos:]
                    self.wr(res[pos])
                    pos += len(char)
                    push_msg(res[pos:]) 

    def find_in_file(self, pattern, col, end):
        try: from ure import compile
        except: from re import compile
        Editor.find_pattern = pattern 
        if Editor.case != "y":
            pattern = pattern.lower()
        try:
            rex = compile(pattern)
        except:
            self.message = "Invalid pattern: " + pattern
            return -1
        scol = col
        for line in range(self.cur_line, end):
            l = self.content[line]
            if Editor.case != "y":
                l = l.lower()
            ecol = 1 if pattern[0] == '^' else len(l) + 1
            for i in range(scol, ecol):
                match = rex.match(l[i:])
                if match: 
                    self.col = i
                    self.cur_line = line
                    return len(match.group(0))
            scol = 0
        else:
            self.message = pattern + " not found"
            return -1

    def undo_add(self, lnum, text, key, span = 1):
        self.changed = '*'
        if self.undo_limit > 0 and (
           len(self.undo) == 0 or key == 0 or self.undo[-1][3] != key or self.undo[-1][0] != lnum):
            if len(self.undo) >= self.undo_limit: 
                del self.undo[0]
                self.undo_zero -= 1
            self.undo.append([lnum, span, text, key, self.col])

    def delete_lines(self, yank): 
        lrange = self.line_range()
        if yank:
            Editor.yank_buffer = self.content[lrange[0]:lrange[1]]
        self.undo_add(lrange[0], self.content[lrange[0]:lrange[1]], 0, 0) 
        del self.content[lrange[0]:lrange[1]]
        if self.content == []: 
            self.content = [""] 
            self.undo[-1][1] = 1 
        self.total_lines = len(self.content)
        self.cur_line = lrange[0]
        self.mark = None 

    def handle_edit_keys(self, key, char): 
        l = self.content[self.cur_line]
        if key == 0x0d:
            if self.cur_line < self.total_lines - 1:
                self.cur_line += 1
                if self.cur_line == self.top_line + Editor.height:
                    self.scroll_down(1)
        elif key == 0x0b:
            if self.cur_line > 0:
                self.cur_line -= 1
                if self.cur_line < self.top_line:
                    self.scroll_up(1)
        elif key == 0x1f:
            if self.col == 0 and self.cur_line > 0:
                self.cur_line -= 1
                self.col = len(self.content[self.cur_line])
                if self.cur_line < self.top_line:
                    self.scroll_up(1)
            else:
                self.col -= 1
        elif key == 0x1e:
            if self.col >= len(l) and self.cur_line < self.total_lines - 1:
                self.col = 0
                self.cur_line += 1
                if self.cur_line == self.top_line + Editor.height:
                    self.scroll_down(1)
            else:
                self.col += 1
        elif key == 0x7f:
            if self.mark != None:
                self.delete_lines(False)
            elif self.col < len(l):
                self.undo_add(self.cur_line, [l], 0x7f)
                self.content[self.cur_line] = l[:self.col] + l[self.col + 1:]
            elif (self.cur_line + 1) < self.total_lines: 
                self.undo_add(self.cur_line, [l, self.content[self.cur_line + 1]], 0)
                self.content[self.cur_line] = l + self.content.pop(self.cur_line + 1)
                self.total_lines -= 1
        elif key == 0x08:
            if self.mark != None:
                self.delete_lines(False)
            elif self.col > 0:
                self.undo_add(self.cur_line, [l], 0x08)
                self.content[self.cur_line] = l[:self.col - 1] + l[self.col:]
                self.col -= 1
            elif self.cur_line > 0: 
                self.undo_add(self.cur_line - 1, [self.content[self.cur_line - 1], l], 0)
                self.col = len(self.content[self.cur_line - 1])
                self.content[self.cur_line - 1] += self.content.pop(self.cur_line)
                self.cur_line -= 1
                self.total_lines -= 1
        elif key == 0: 
            self.mark = None
            self.undo_add(self.cur_line, [l], 0x20 if char == " " else 0x41)
            self.content[self.cur_line] = l[:self.col] + char + l[self.col:]
            self.col += len(char)
        elif key == 0x10:
            ni = self.spaces(l)
            self.col = ni if self.col != ni else 0
        elif key == 0x03:
            self.col = len(l)
        elif key == 0xfff1:
            self.cur_line -= Editor.height
        elif key == 0xfff2:
            self.cur_line += Editor.height
        elif key == 0x06:
            pat = self.line_edit("Find: ", Editor.find_pattern)
            if pat:
                self.find_in_file(pat, self.col, self.total_lines)
                self.row = Editor.height >> 1
        elif key == 0x0e:
            if Editor.find_pattern:
                self.find_in_file(Editor.find_pattern, self.col + 1, self.total_lines)
                self.row = Editor.height >> 1
        elif key == 0x07: 
            line = self.line_edit("Goto Line: ", "")
            if line:
                self.cur_line = int(line) - 1
                self.row = Editor.height >> 1
        elif key == 0x14: 
            self.cur_line = 0
        elif key == 0x02: 
            self.cur_line = self.total_lines - 1
            self.row = Editor.height - 1 
        elif key == 0x01: 
            pat = self.line_edit("Case Sensitive Search {}, Autoindent {}"
            ", Tab Size {}, Write Tabs {}"
            ": ".format(Editor.case, self.autoindent
            , self.tab_size, self.write_tabs
            ), "")
            try:
                res = [i.strip().lower() for i in pat.split(",")]
                if res[0]: Editor.case = 'y' if res[0][0] == 'y' else 'n'
                if res[1]: self.autoindent = 'y' if res[1][0] == 'y' else 'n'
                if res[2]: self.tab_size = int(res[2])
                if res[3]: self.write_tabs = 'y' if res[3][0] == 'y' else 'n'
            except:
                pass
        elif key == 0x1b: 
            if char[1] < Editor.height:
                self.col = char[0] + self.margin
                self.cur_line = char[1] + self.top_line
                if char[2] in (0x22, 0x30): 
                    self.mark = self.cur_line if self.mark == None else None
        elif key == 0x1c: 
            if self.top_line > 0:
                self.top_line = max(self.top_line - 3, 0)
                self.cur_line = min(self.cur_line, self.top_line + Editor.height - 1)
                self.scroll_up(3)
        elif key == 0x1d: 
            if self.top_line + Editor.height < self.total_lines:
                self.top_line = min(self.top_line + 3, self.total_lines - 1)
                self.cur_line = max(self.cur_line, self.top_line)
                self.scroll_down(3)
        elif key == 0xfffd:
            if self.col < len(l): 
                opening = "([{<"
                closing = ")]}>"
                level = 0
                pos = self.col
                srch = l[pos]
                i = opening.find(srch)
                if i >= 0: 
                    pos += 1
                    match = closing[i]
                    for i in range(self.cur_line, self.total_lines):
                        for c in range(pos, len(self.content[i])):
                            if self.content[i][c] == match:
                                if level == 0: 
                                    self.cur_line, self.col = i, c
                                    return True 
                                else:
                                    level -= 1
                            elif self.content[i][c] == srch:
                                level += 1
                        pos = 0 
                else:
                    i = closing.find(srch)
                    if i >= 0: 
                        pos -= 1
                        match = opening[i]
                        for i in range(self.cur_line, -1, -1):
                            for c in range(pos, -1, -1):
                                if self.content[i][c] == match:
                                    if level == 0: 
                                        self.cur_line, self.col = i, c
                                        return True 
                                    else:
                                        level -= 1
                                elif self.content[i][c] == srch:
                                    level += 1
                            if i > 0: 
                                pos = len(self.content[i - 1]) - 1
        elif key == 0x0c:
            self.mark = self.cur_line if self.mark == None else None
        elif key == 0x0a:
            self.mark = None
            self.undo_add(self.cur_line, [l], 0, 2)
            self.content[self.cur_line] = l[:self.col]
            ni = 0
            if self.autoindent == "y": 
                ni = min(self.spaces(l), self.col) 
            self.cur_line += 1
            self.content[self.cur_line:self.cur_line] = [' ' * ni + l[self.col:]]
            self.total_lines += 1
            self.col = ni
        elif key == 0x09:
            if self.mark == None:
                ni = self.tab_size - self.col % self.tab_size 
                self.undo_add(self.cur_line, [l], 0x09)
                self.content[self.cur_line] = l[:self.col] + ' ' * ni + l[self.col:]
                self.col += ni
            else:
                lrange = self.line_range()
                self.undo_add(lrange[0], self.content[lrange[0]:lrange[1]], 0xfffe, lrange[1] - lrange[0]) 
                for i in range(lrange[0],lrange[1]):
                    if len(self.content[i]) > 0:
                        self.content[i] = ' ' * (self.tab_size - self.spaces(self.content[i]) % self.tab_size) + self.content[i]
        elif key == 0x15:
            if self.mark == None:
                ni = min((self.col - 1) % self.tab_size + 1, self.spaces(l, self.col)) 
                if ni > 0:
                    self.undo_add(self.cur_line, [l], 0x15)
                    self.content[self.cur_line] = l[:self.col - ni] + l[self.col:]
                    self.col -= ni
            else:
                lrange = self.line_range()
                self.undo_add(lrange[0], self.content[lrange[0]:lrange[1]], 0xffff, lrange[1] - lrange[0]) 
                for i in range(lrange[0],lrange[1]):
                    ns = self.spaces(self.content[i])
                    if ns > 0:
                        self.content[i] = self.content[i][(ns - 1) % self.tab_size + 1:]
        elif key == 0x12:
            count = 0
            pat = self.line_edit("Replace: ", Editor.find_pattern)
            if pat:
                rpat = self.line_edit("With: ", Editor.replc_pattern)
                if rpat != None: 
                    Editor.replc_pattern = rpat
                    q = ''
                    cur_line = self.cur_line 
                    if self.mark != None: 
                        (self.cur_line, end_line) = self.line_range()
                        self.col = 0
                    else: 
                        end_line = self.total_lines
                    self.message = "Replace (yes/No/all/quit) ? "
                    while True: 
                        ni = self.find_in_file(pat, self.col, end_line)
                        if ni >= 0: 
                            if q != 'a':
                                self.display_window()
                                key, char = self.get_input() 
                                q = char.lower()
                            if q == 'q' or key == 0x11:
                                break
                            elif q in ('a','y'):
                                self.undo_add(self.cur_line, [self.content[self.cur_line]], 0)
                                self.content[self.cur_line] = self.content[self.cur_line][:self.col] + rpat + self.content[self.cur_line][self.col + ni:]
                                self.col += len(rpat)
                                count += 1
                            else: 
                                 self.col += 1
                            if self.col >= len(self.content[self.cur_line]): 
                                self.cur_line += 1
                                self.col = 0
                        else: 
                            break
                    self.cur_line = cur_line 
                    self.message = "'{}' replaced {} times".format(pat, count)
        elif key == 0x18: 
            if self.mark != None: self.delete_lines(True)
        elif key == 0x04: 
            if self.mark != None:
                lrange = self.line_range()
                Editor.yank_buffer = self.content[lrange[0]:lrange[1]]
                self.mark = None
        elif key == 0x16: 
            if Editor.yank_buffer:
                if self.mark != None: self.delete_lines(False)
                self.undo_add(self.cur_line, None, 0, -len(Editor.yank_buffer))
                self.content[self.cur_line:self.cur_line] = Editor.yank_buffer 
                self.total_lines += len(Editor.yank_buffer)
        elif key == 0x13:
            fname = self.line_edit("Save File: ", self.fname)
            if fname:
                self.put_file(fname)
                self.changed = '' 
                self.undo_zero = len(self.undo) 
                self.fname = fname 
        elif key == 0x1a:
            if len(self.undo) > 0:
                action = self.undo.pop(-1) 
                if not action[3] in (0xfffe, 0xffff):
                    self.cur_line = action[0] 
                self.col = action[4]
                if action[1] >= 0: 
                    if action[0] < self.total_lines:
                        self.content[action[0]:action[0] + action[1]] = action[2] 
                    else:
                        self.content += action[2]
                else: 
                    del self.content[action[0]:action[0] - action[1]]
                self.total_lines = len(self.content) 
                if len(self.undo) == self.undo_zero: self.changed = ''
                self.mark = None
        elif key == 0x05:
            self.redraw(True)

    def edit_loop(self): 
        if not self.content: 
            self.content = [""]
        self.total_lines = len(self.content)
        self.redraw(self.message == "")
        while True:
            if not self.rd_any(): 
                self.display_window() 
            key, char = self.get_input() 
            self.message = '' 
            if key == 0x11:
                if self.changed:
                    res = self.line_edit("Content changed! Quit without saving (y/N)? ", "N")
                    if not res or res[0].upper() != 'Y':
                        continue
                self.scroll_region(0)
                self.mouse_reporting(False) 
                self.goto(Editor.height, 0)
                self.clear_to_eol()
                self.undo = []
                return key
            elif key in (0x17, 0x0f):
                return key
            else:
                self.handle_edit_keys(key, char)

    def packtabs(self, s):
        try: from uio import StringIO
        except: from _io import StringIO
        sb = StringIO()
        for i in range(0, len(s), 8):
            c = s[i:i + 8]
            cr = c.rstrip(" ")
            if (len(c) - len(cr)) > 1:
                sb.write(cr + "\t") 
            else: sb.write(c)
        return sb.getvalue()

    def get_file(self, fname):
        from os import listdir
        try: from uos import stat
        except: from os import stat
        if not fname:
            fname = self.line_edit("Open file: ", "")
        if fname:
            self.fname = fname
            if fname in ('.', '..') or (stat(fname)[0] & 0x4000): 
                self.content = ["Directory '{}'".format(fname), ""] + sorted(listdir(fname))
            else:
                if True:
                    with open(fname) as f:
                        self.content = f.readlines()
                Editor.tab_seen = 'n'
                for i, l in enumerate(self.content):
                    self.content[i] = expandtabs(l.rstrip('\r\n\t '))
                self.write_tabs = Editor.tab_seen

    def put_file(self, fname):
        if True:
            from uos import remove, rename
        tmpfile = fname + ".pyetmp"
        with open(tmpfile, "w") as f:
            for l in self.content:
                if self.write_tabs == 'y':
                    f.write(self.packtabs(l) + '\n')
                else:
                    f.write(l + '\n')
        try: remove(fname)
        except: pass
        rename(tmpfile, fname)

def expandtabs(s):
    try: from uio import StringIO
    except: from _io import StringIO
    if '\t' in s:
        Editor.tab_seen = 'y'
        sb = StringIO()
        pos = 0
        for c in s:
            if c == '\t': 
                sb.write(" " * (8 - pos % 8)) 
                pos += 8 - pos % 8
            else:
                sb.write(c)
                pos += 1
        return sb.getvalue()
    else:
        return s

def pye(*content, tab_size = 4, undo = 50, device = 0, baud = 115200):
    gc.collect() 
    slot = [Editor(tab_size, undo)]
    index = 0
    if content:
        for f in content:
            if index: slot.append(Editor(tab_size, undo))
            if type(f) == str and f: 
                try:
                    slot[index].get_file(f)
                except Exception as err:
                    slot[index].message = "{!r}".format(err)
            elif type(f) == list and len(f) > 0 and type(f[0]) == str:
                slot[index].content = f 
            index += 1

    while True:
        try:
            index %= len(slot)
            key = slot[index].edit_loop() 
            if key == 0x11:
                if len(slot) == 1: 
                    break
                del slot[index]
            elif key == 0x0f:
                slot.append(Editor(tab_size, undo))
                index = len(slot) - 1
                slot[index].get_file(None)
            elif key == 0x17:
                index += 1
        except Exception as err:
            slot[index].message = "{!r}".format(err)
    Editor.yank_buffer = []
    return slot[0].content if (slot[0].fname == "") else slot[0].fname
