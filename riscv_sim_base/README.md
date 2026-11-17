# RV32I Base Simulator (with 5-stage pipeline)

A single-cycle RV32I RISC-V simulator, assembler, and CLI debugger written in
portable C11, extended over the labs with a five-stage pipeline, hazard
handling (stall / forwarding), and branch prediction. Later labs add the M/F
extensions and a custom-extension capstone.

Every lab extends the system by **adding new files** that plug into the
interfaces described below. The single-cycle engine stays in the build as the
reference **oracle**: every pipeline mode must finish with the same registers
and printed output as the oracle.

> Lab numbering used by the lab sheets: Lab 2 = pipeline (W7 sheet),
> Lab 3 = data hazards, Lab 4 = branch prediction, Lab 5 = floating point (RV32F).

## Why it's built this way

The whole codebase hinges on one idea: instructions are not hardcoded into
the simulator. They're entries in a shared **ISA registry**
(`core/isa_registry.h`) — a table of `{mnemonic, opcode, funct3, funct7,
format, execute-function}` structs. The decoder looks instructions up in
this table by their bit pattern; the assembler looks the same table up by
mnemonic. Because both share one source of truth, adding an instruction
(e.g. `MUL` in the M-extension lab) means writing one new file that calls
`register_instruction(...)` — the core simulator, decoder, and assembler
never need to change.

If the decoder can't find a matching entry, it reports
`ILLEGAL_INSTRUCTION` with the offending opcode and PC. The pipeline carries
that fault down the pipe and only raises it in WB (precise exceptions).

## Directory structure

```
riscv_sim_base/
├── core/                   # engine internals — do not modify in labs
│   ├── cpu_state.h/.c      # register file, PC, CPUStatus (RUNNING/HALTED/...)
│   ├── memory.h/.c         # flat byte-addressable memory, little-endian load/store
│   ├── isa_registry.h/.c   # the instruction dispatch table (the plugin mechanism)
│   ├── decoder.h/.c        # raw 32-bit word -> DecodedInstr, via the registry
│   └── executor.h/.c       # single-cycle fetch/decode/execute loop (the oracle)
│
├── isa/
│   └── rv32i_base.h/.c     # base RV32I instructions, registered at startup
│                           # (Lab 5 adds isa/rv32f.c)
│
├── assembler/
│   └── assembler.h/.c      # two-pass assembler; shares core/isa_registry.h
│
├── pipeline/               # the five-stage pipeline (Labs 2-4)
│   ├── control.h/.c        # Lab 2: ControlSignals + control_decode()
│   ├── pipeline_state.h    # Lab 2-4: IF/ID, ID/EX, EX/MEM, MEM/WB latches,
│   │                       #   Pipeline struct and statistics counters
│   ├── pipeline.c          # the five stages, stalls, flushes, statistics
│   ├── hazard.h/.c         # Lab 3: hazard_check(), forward_operands()
│   ├── bp.c                # Lab 4: the four branch predictors (in use)
│   └── bp_static.c, branch_predictor.h/.c
│                           # sheet-style plug-in predictor interface; present
│                           # but NOT part of the build (see "Known state")
│
├── ui/
│   └── cli_display.h/.c    # register/memory/status display + step-debugger REPL
│
├── main.c                  # wiring: registers the ISA, parses options, runs
├── Makefile
├── run_sample_tests.sh     # runs the sample_tests suite against expected output
├── tests/                  # example programs
└── README.md
```

## Building

Requires `gcc` (or any C11 compiler) and `make`. No external dependencies.

```bash
cd riscv_sim_base
make
```

This produces an `rvsim` binary in the project root. `make clean` removes
build artifacts.

**Important:** the Makefile does not track header dependencies. After editing
any `.h` file (e.g. `pipeline_state.h`), run `make clean && make`, otherwise
stale object files with an old struct layout are linked and results are
silently wrong (for example every predictor behaving like "no predictor").

Filenames are case-sensitive on Linux/macOS: keep `hazard.c` / `hazard.h`
lowercase to match the `#include` lines.

## Running

```bash
./rvsim <program.s>                 # interactive step-debugger (default)
./rvsim <program.s> --batch         # single-cycle oracle: run to completion, dump state
./rvsim <program.s> --pipeline      # 5-stage pipeline, stall-only hazards
./rvsim <program.s> --pipeline-forward   # pipeline with forwarding + load-use stall
./rvsim <program.s> --max-cycles 500000
./rvsim <program.s> --mem-size 2097152   # override the default 1 MB memory
```

### Options

Both the original and the lab-sheet spellings are accepted:

| Option | Meaning |
|---|---|
| `--batch` or `--single-cycle` | single-cycle oracle, run to completion |
| `--pipeline` or `--hazard stall` | pipeline, stall on a producer in EX or MEM; `ecall` waits for `a7`/`a0` |
| `--pipeline-forward` or `--hazard forward` | pipeline with EX/MEM and MEM/WB forwarding plus one-cycle load-use stall |
| `--hazard none` | pipeline with no hazard handling at all (only correct on code padded with `nop`s); `ecall` uses the `a7`/`a0` values read in ID |
| `--predict <name>` or `--predictor <name>` | branch predictor: `not-taken`, `btfn`, `1bit`, `2bit`. Implies forwarding (a warning is printed) |
| `--max-cycles N` | cycle limit (default 1,000,000) |
| `--mem-size N` | memory size in bytes (default 1 MB) |

Not implemented: `--trace`, and `--predictor` together with `--hazard stall`
(the simulator switches to forwarding and warns).

### Pipeline statistics

A pipeline run ends with a block such as:

```
pipeline        = 5-stage, hazard unit 'forward', predictor '2bit'
retired         = 37
CPI             = 1.216
stalls          = 0
forwards        = 20
branches        = 10
mispredicts     = 2
accuracy        = 80.0%
jal             = 0
jalr            = 0
redirects       = 2
squashed        = 4
```

Identity that must hold for every program and mode:

```
cycles = retired + 4 + 2 x redirects + stalls
redirects = mispredicts + jalr        (when a predictor is selected)
```

`retired` must equal the single-cycle `cycles` count for the same program.

### Design rules the pipeline follows

- All stages read the old latches and write the new ones (snapshot/commit).
- A stall is three actions: a bubble enters EX, IF/ID is frozen, IF does not fetch.
- Stall policy: a producer in EX or MEM stalls the consumer in ID. A producer
  in WB does not, because WB writes the register file before ID reads it in
  the same cycle.
- Forwarding priority: EX/MEM beats MEM/WB. Forwarded values feed the ALU,
  store data, branch comparison and the `jalr` target.
- Load-use is the one hazard forwarding cannot remove: exactly one stall,
  detected from `ctrl.mem_read` in ID/EX.
- Branches resolve in EX; a misprediction squashes IF and ID (2 cycles).
  `jal` is redirected in IF when a predictor is selected; `jalr` is never
  predicted (2 cycles each).
- Predictors see only the branch PC and target. The table has 64 entries,
  indexed by `pc[7:2]`, with no tags. 2-bit counters saturate and start at 1
  (weakly not taken).

### Interactive mode

```
$ ./rvsim tests/sample1_sum_loop.s
RISC-V single-cycle simulator -- interactive mode ('help' for commands)
status = RUNNING   cycles = 0
(sim) step 5
(sim) regs
(sim) mem 0x38 16
(sim) break 0x34
(sim) run
(sim) quit
```

Commands: `step [n]`, `run`, `regs`, `mem <addr> <len>`, `break <addr>`,
`clearbreak <addr>`, `status`, `help`, `quit`/`exit`. Interactive mode runs
the single-cycle engine only.

## Testing

```bash
make test                                   # quick smoke test of the sample programs
bash run_sample_tests.sh ../sample_tests    # full check against the expected outputs
```

`run_sample_tests.sh` compares printed output, registers and cycle counts for
every program in `sample_tests/` against its expected file, in single-cycle,
`none`, `stall`, `forward` and the four predictor modes. Current expected
result: all Lab 2-4 rows pass; Lab 5 (floating point) rows fail until
`isa/rv32f.c` exists.

Example programs in `tests/`: `sample1_sum_loop.s`, `sample2_mem_and_branches.s`,
`sample3_hazards.s`, `micro.s`, `sample4_data_hazards.s` (hazard labs) and
`t1_counted_loop.s`, `t2_nested_loops.s`, `t3_alternating.s` (branch labs).

## Assembly language supported

**Base RV32I:** `add sub sll slt sltu xor srl sra or and` `addi slti sltiu
xori ori andi slli srli srai` `lb lh lw lbu lhu` `sb sh sw` `beq bne blt bge
bltu bgeu` `jal jalr lui auipc` `ecall`

**Pseudo-instructions:** `nop`, `ret`, `j label`, `mv rd, rs`, `li rd, imm`
(always expands to `lui`+`addi`, i.e. always 8 bytes; `li` also accepts a
label), `la`.

**Directives:** `.text`, `.data`, `.globl name`, `.word v1[, v2...]`,
`.half v1[, v2...]`, `.byte v1[, v2...]` (each operand may be a numeric
literal or a label), `.align n`.

**Registers:** both `x0`–`x31` and ABI names (`zero ra sp gp tp t0-t6 s0-s11
a0-a7`, plus `fp` as an alias for `s0`).

**Syscall convention (`ecall`)**, deliberately minimal:
- `a7 = 1`: print the signed integer in `a0` to stdout
- `a7 = 10`: exit (halts the machine with status `ECALL_EXIT`)
- anything else: ignored, with a warning printed to stderr

**Program entry point:** code is placed at address 0 and execution starts at
`main:` if it exists, otherwise address 0.

## Error handling conventions (for anyone extending this code)

- No exceptions in C: every fallible function returns a status enum
  (`MemStatus`, `DecodeStatus`, `AsmStatus`, `CPUStatus`). Check return
  values; don't call `exit()` from `core/`, `isa/`, or `assembler/`.
- Unknown instructions are a normal outcome: decode failure sets
  `CPUStatus = CPU_ILLEGAL_INSTRUCTION` with the offending opcode and PC.
- Out-of-bounds memory accesses return `MEM_ERR_OUT_OF_BOUNDS`; the executor
  turns that into `CPU_MEM_FAULT`. In the pipeline the fault travels in the
  latch and becomes visible only in WB.
- Assembler errors (`AsmError`) always carry a source line number.

## Extending the ISA registry

Adding an instruction is: write an `execute` function with the signature
`void fn(CPUState *cpu, Memory *mem, DecodedInstr *instr)`, then register it:

```c
static void exec_mul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1);
    int32_t b = (int32_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)(a * b));
}

void register_rv32m(void) {
    register_instruction((InstructionHandler){"mul", 0x33, 0x0, 0x01, FMT_R, exec_mul});
}
```

Then in `main.c`, alongside `register_rv32i_base();`, add `register_rv32m();`.
The assembler and the pipeline pick up the new instruction automatically.
Floating point (Lab 5) uses `register_instruction_ex` with operand flags
(`IF_RD_FP`, `IF_RS1_FP`, `IF_HAS_RM`, ...) in the same way, but needs the
Lab 5 starter infrastructure (second register file, `fcsr`, helpers).

## Known state

- The active predictor code is `pipeline/bp.c`. The sheet-style plug-in
  files (`bp_static.c`, `branch_predictor.*`) are not linked.
- `t4_calls.s` (Lab 4) is not part of the handout.
- Lab 5 (RV32F) is not implemented yet.

## A note on `valgrind`

```bash
make memcheck PROG=tests/sample1_sum_loop.s
```

Requires `valgrind` installed locally; unverified in the authoring environment.