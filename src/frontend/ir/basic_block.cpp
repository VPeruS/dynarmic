/* This file is part of the dynarmic project.
 * Copyright (c) 2016 MerryMage
 * This software may be used and distributed according to the terms of the GNU
 * General Public License version 2 or any later version.
 */

#include <algorithm>
#include <initializer_list>
#include <map>
#include <string>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "common/assert.h"
#include "frontend/A32/types.h"
#include "frontend/A64/types.h"
#include "frontend/ir/basic_block.h"
#include "frontend/ir/opcodes.h"

namespace Dynarmic::IR {

void Block::AppendNewInst(Opcode opcode, std::initializer_list<IR::Value> args) {
    PrependNewInst(end(), opcode, args);
}

Block::iterator Block::PrependNewInst(iterator insertion_point, Opcode opcode, std::initializer_list<Value> args) {
    IR::Inst* inst = new(instruction_alloc_pool->Alloc()) IR::Inst(opcode);
    ASSERT(args.size() == inst->NumArgs());

    std::for_each(args.begin(), args.end(), [&inst, index = size_t(0)](const auto& arg) mutable {
        inst->SetArg(index, arg);
        index++;
    });

    return instructions.insert_before(insertion_point, inst);
}

LocationDescriptor Block::Location() const {
    return location;
}

LocationDescriptor Block::EndLocation() const {
    return end_location;
}

void Block::SetEndLocation(const LocationDescriptor& descriptor) {
    end_location = descriptor;
}

Cond Block::GetCondition() const {
    return cond;
}

void Block::SetCondition(Cond condition) {
    cond = condition;
}

LocationDescriptor Block::ConditionFailedLocation() const {
    return cond_failed.value();
}

void Block::SetConditionFailedLocation(LocationDescriptor fail_location) {
    cond_failed = fail_location;
}

size_t& Block::ConditionFailedCycleCount() {
    return cond_failed_cycle_count;
}

const size_t& Block::ConditionFailedCycleCount() const {
    return cond_failed_cycle_count;
}

bool Block::HasConditionFailedLocation() const {
    return cond_failed.has_value();
}

Block::InstructionList& Block::Instructions() {
    return instructions;
}

const Block::InstructionList& Block::Instructions() const {
    return instructions;
}

Terminal Block::GetTerminal() const {
    return terminal;
}

void Block::SetTerminal(Terminal term) {
    ASSERT_MSG(!HasTerminal(), "Terminal has already been set.");
    terminal = term;
}

void Block::ReplaceTerminal(Terminal term) {
    ASSERT_MSG(HasTerminal(), "Terminal has not been set.");
    terminal = term;
}

bool Block::HasTerminal() const {
    return terminal.which() != 0;
}

size_t& Block::CycleCount() {
    return cycle_count;
}

const size_t& Block::CycleCount() const {
    return cycle_count;
}

static std::string TerminalToString(const Terminal& terminal_variant) {
    switch (terminal_variant.which()) {
    case 1: {
        auto terminal = boost::get<IR::Term::Interpret>(terminal_variant);
        return fmt::format("Interpret{{{}}}", terminal.next);
    }
    case 2: {
        return "ReturnToDispatch{}";
    }
    case 3: {
        auto terminal = boost::get<IR::Term::LinkBlock>(terminal_variant);
        return fmt::format("LinkBlock{{{}}}", terminal.next);
    }
    case 4: {
        auto terminal = boost::get<IR::Term::LinkBlockFast>(terminal_variant);
        return fmt::format("LinkBlockFast{{{}}}", terminal.next);
    }
    case 5: {
        return "PopRSBHint{}";
    }
    case 6: {
        auto terminal = boost::get<IR::Term::If>(terminal_variant);
        return fmt::format("If{{{}, {}, {}}}", A64::CondToString(terminal.if_), TerminalToString(terminal.then_), TerminalToString(terminal.else_));
    }
    case 7: {
        auto terminal = boost::get<IR::Term::CheckBit>(terminal_variant);
        return fmt::format("CheckBit{{{}, {}}}", TerminalToString(terminal.then_), TerminalToString(terminal.else_));
    }
    case 8: {
        auto terminal = boost::get<IR::Term::CheckHalt>(terminal_variant);
        return fmt::format("CheckHalt{{{}}}", TerminalToString(terminal.else_));
    }
    default:
        return "<invalid terminal>";
    }
}

std::string DumpBlock(const IR::Block& block) {
    std::string ret;

    ret += fmt::format("Block: location={}\n", block.Location());
    ret += fmt::format("cycles={}", block.CycleCount());
    ret += fmt::format(", entry_cond={}", A64::CondToString(block.GetCondition()));
    if (block.GetCondition() != Cond::AL) {
        ret += fmt::format(", cond_fail={}", block.ConditionFailedLocation());
    }
    ret += '\n';

    std::map<const IR::Inst*, size_t> inst_to_index;
    size_t index = 0;

    const auto arg_to_string = [&inst_to_index](const IR::Value& arg) -> std::string {
        if (arg.IsEmpty()) {
            return "<null>";
        } else if (!arg.IsImmediate()) {
            return fmt::format("%{}", inst_to_index.at(arg.GetInst()));
        }
        switch (arg.GetType()) {
        case Type::U1:
            return fmt::format("#{}", arg.GetU1() ? '1' : '0');
        case Type::U8:
            return fmt::format("#{}", arg.GetU8());
        case Type::U16:
            return fmt::format("#{:#x}", arg.GetU16());
        case Type::U32:
            return fmt::format("#{:#x}", arg.GetU32());
        case Type::U64:
            return fmt::format("#{:#x}", arg.GetU64());
        case Type::A32Reg:
            return A32::RegToString(arg.GetA32RegRef());
        case Type::A32ExtReg:
            return A32::ExtRegToString(arg.GetA32ExtRegRef());
        case Type::A64Reg:
            return A64::RegToString(arg.GetA64RegRef());
        case Type::A64Vec:
            return A64::VecToString(arg.GetA64VecRef());
        default:
            return "<unknown immediate type>";
        }
    };

    for (const auto& inst : block) {
        const Opcode op = inst.GetOpcode();

        ret += fmt::format("[{:016x}] ", reinterpret_cast<u64>(&inst));
        if (GetTypeOf(op) != Type::Void) {
            ret += fmt::format("%{:<5} = ", index);
        } else {
            ret += "         "; // '%00000 = ' -> 1 + 5 + 3 = 9 spaces
        }

        ret += GetNameOf(op);

        const size_t arg_count = GetNumArgsOf(op);
        for (size_t arg_index = 0; arg_index < arg_count; arg_index++) {
            const Value arg = inst.GetArg(arg_index);

            ret += arg_index != 0 ? ", " : " ";
            ret += arg_to_string(arg);

            Type actual_type = arg.GetType();
            Type expected_type = GetArgTypeOf(op, arg_index);
            if (!AreTypesCompatible(actual_type, expected_type)) {
                ret += fmt::format("<type error: {} != {}>", GetNameOf(actual_type), GetNameOf(expected_type));
            }
        }

        ret += fmt::format(" (uses: {})", inst.UseCount());

        ret += '\n';
        inst_to_index[&inst] = index++;
    }

    ret += "terminal = " + TerminalToString(block.GetTerminal()) + '\n';

    return ret;
}

} // namespace Dynarmic::IR
