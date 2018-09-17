"""
ESP32 ULP Co-Processor Assembler
"""

from . import opcodes
from .nocomment import remove_comments
from .util import garbage_collect

TEXT, DATA, BSS = 'text', 'data', 'bss'

REL, ABS = 0, 1


class SymbolTable:
    def __init__(self, symbols, bases):
        self._symbols = symbols
        self._bases = bases
        self._pass = None

    def set_pass(self, _pass):
        self._pass = _pass

    def set_bases(self, bases):
        self._bases = bases

    def set_from(self, from_section, from_offset):
        self._from_section, self._from_offset = from_section, from_offset

    def get_from(self):
        return self._from_section, self._from_offset

    def set_sym(self, symbol, stype, section, value):
        entry = (stype, section, value)
        if symbol in self._symbols and entry != self._symbols[symbol]:
            raise Exception('redefining symbol %s with different value %r -> %r.' % (label, self._symbols[symbol], entry))
        self._symbols[symbol] = entry

    def has_sym(self, symbol):
        return symbol in self._symbols
        
    def get_sym(self, symbol):
        try:
            entry = self._symbols[symbol]
        except KeyError:
            if self._pass == 1:
                entry = (REL, TEXT, 0)  # for a dummy, this is good enough
            else:
                raise
        return entry

    def dump(self):
        for symbol, entry in self._symbols.items():
            print(symbol, entry)

    def export(self):
        addrs_syms = [(self.resolve_absolute(entry), symbol) for symbol, entry in self._symbols.items()]
        return sorted(addrs_syms)

    def to_abs_addr(self, section, offset):
        try:
            base = self._bases[section]
        except KeyError:
            if self._pass == 1:
                base = 0  # for a dummy this is good enough
            else:
                raise
        return base + offset

    def resolve_absolute(self, symbol):
        if isinstance(symbol, str):
            stype, section, value = self.get_sym(symbol)
        elif isinstance(symbol, tuple):
            stype, section, value = symbol
        else:
            raise TypeError
        if stype == REL:
            return self.to_abs_addr(section, value)
        if stype == ABS:
            return value
        raise TypeError(stype)

    def resolve_relative(self, symbol):
        if isinstance(symbol, str):
            sym_type, sym_section, sym_value = self.get_sym(symbol)
        elif isinstance(symbol, tuple):
            sym_type, sym_section, sym_value = symbol
        else:
            raise TypeError
        if sym_type == REL:
            sym_addr = self.to_abs_addr(sym_section, sym_value)
        elif sym_type == ABS:
            sym_addr = sym_value
        from_addr = self.to_abs_addr(self._from_section, self._from_offset)
        return sym_addr - from_addr


class Assembler:

    def __init__(self, symbols=None, bases=None):
        self.symbols = SymbolTable(symbols or {}, bases or {})
        opcodes.symbols = self.symbols  # XXX dirty hack

    def init(self, a_pass):
        self.a_pass = a_pass
        self.symbols.set_pass(a_pass)
        self.sections = dict(text=[], data=[])
        self.offsets = dict(text=0, data=0, bss=0)
        self.section = TEXT

    def parse_line(self, line):
        """
        parse one line of assembler into label, opcode, args.
        comments already have been removed by pre-processing.

        a line looks like (label, opcode, args, comment are all optional):

        label:    opcode arg1, arg2, ...
        """
        if not line:
            return
        has_label = line[0] not in '\t '
        if has_label:
            label_line = line.split(None, 1)
            if len(label_line) == 2:
                label, line = label_line
            else:  # 1
                label, line = label_line[0], None
            label = label.rstrip(':')
        else:
            label, line = None, line.lstrip()
        if line is None:
            opcode, args = None, ()
        else:
            opcode_args = line.split(None, 1)
            if len(opcode_args) == 2:
                opcode, args = opcode_args
                args = tuple(arg.strip() for arg in args.split(','))
            else:  # 1
                opcode, args = opcode_args[0], ()
        return label, opcode, args


    def parse(self, lines):
        parsed = [self.parse_line(line) for line in lines]
        return [p for p in parsed if p is not None]


    def append_section(self, value, expected_section=None):
        s = self.section
        if expected_section is not None and s is not expected_section:
            raise TypeError('only allowed in %s section' % expected_section)
        if s is BSS:
            # just increase BSS size by value
            self.offsets[s] += value
        else:
            self.sections[s].append(value)
            self.offsets[s] += len(value)

    def finalize_sections(self):
        # make sure all sections have a bytelength dividable by 4,
        # thus having all sections aligned at 32bit-word boundaries.
        for s in list(self.sections.keys()) + [BSS, ]:
            offs = self.offsets[s]
            mod = offs % 4
            if mod:
                fill = int(0).to_bytes(4 - mod, 'little')
                self.offsets[s] += len(fill)
                if s is not BSS:
                    self.sections[s].append(fill)

    def compute_bases(self):
        bases = {}
        addr = 0
        # lay out sections in this order:
        for s in [TEXT, DATA, BSS]:  # TODO: more flexibility for custom sections
            bases[s] = addr
            addr += self.offsets[s] // 4  # 32bit word addresses
        return bases

    def dump(self):
        print("Symbols:")
        self.symbols.dump()
        print("%s section:" % TEXT)
        for t in self.sections[TEXT]:
            print("%08x" % int.from_bytes(t, 'little'))
        print("size: %d" % self.offsets[TEXT])
        print("%s section:" % DATA)
        for d in self.sections[DATA]:
            print("%08x" % int.from_bytes(d, 'little'))
        print("size: %d" % self.offsets[DATA])
        print("%s section:" % BSS)
        print("size: %d" % self.offsets[BSS])

    def fetch(self):
        def get_bytes(section):
            return b''.join(self.sections[section])

        return get_bytes(TEXT), get_bytes(DATA), self.offsets[BSS]

    def d_text(self):
        self.section = TEXT

    def d_data(self):
        self.section = DATA

    def d_bss(self):
        self.section = BSS

    def fill(self, section, amount, fill_byte):
        if fill_byte is not None and section is BSS:
            raise ValueError('fill in bss section not allowed')
        if section is TEXT:  # TODO: text section should be filled with NOPs
            raise ValueError('fill/skip/align in text section not supported')
        fill = int(fill_byte or 0).to_bytes(1, 'little') * amount
        self.offsets[section] += len(fill)
        if section is not BSS:
            self.sections[section].append(fill)

    def d_skip(self, amount, fill=None):
        amount = int(amount)
        self.fill(self.section, amount, fill)

    d_space = d_skip

    def d_align(self, align=4, fill=None):
        align = int(align)
        offs = self.offsets[self.section]
        mod = offs % align
        if mod:
            amount = align - mod
            self.fill(self.section, amount, fill)

    def d_set(self, symbol, expr):
        value = int(expr)  # TODO: support more than just integers
        self.symbols.set_sym(symbol, ABS, None, value)

    def append_data(self, wordlen, args):
        data = [int(arg).to_bytes(wordlen, 'little') for arg in args]
        self.append_section(b''.join(data))

    def d_byte(self, *args):
        self.append_data(1, args)

    def d_word(self, *args):
        self.append_data(2, args)

    def d_long(self, *args):
        self.append_data(4, args)

    def assembler_pass(self, lines):
        for label, opcode, args in self.parse(lines):
            self.symbols.set_from(self.section, self.offsets[self.section] // 4)
            if label is not None:
                self.symbols.set_sym(label, REL, *self.symbols.get_from())
            if opcode is not None:
                if opcode[0] == '.':
                    # assembler directive
                    func = getattr(self, 'd_' + opcode[1:])
                    if func is not None:
                        result = func(*args)
                        if result is not None:
                            self.append_section(result)
                        continue
                else:
                    # machine instruction
                    func = getattr(opcodes, 'i_' + opcode, None)
                    if func is not None:
                        instruction = func(*args)
                        self.append_section(instruction.to_bytes(4, 'little'), TEXT)
                        continue
                raise Exception('Unknown opcode or directive: %s' % opcode)
        self.finalize_sections()

    def assemble(self, text):
        lines = remove_comments(text)
        self.init(1)  # pass 1 is only to get the symbol table right
        self.assembler_pass(lines)
        self.symbols.set_bases(self.compute_bases())
        garbage_collect('before pass2')
        self.init(2)  # now we know all symbols and bases, do the real assembler pass, pass 2
        self.assembler_pass(lines)
        garbage_collect('after pass2')

