/**
 * @file src/bin2llvmir/utils/debug.cpp
 * @brief Debugging utilities.
 * @copyright (c) 2017 Avast Software, licensed under the MIT license
 */

#include <fstream>

#include <llvm/IR/CFG.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Casting.h>

#include "retdec/bin2llvmir/providers/asm_instruction.h"
#include "retdec/bin2llvmir/providers/names.h"
#include "retdec/bin2llvmir/utils/debug.h"
#include "retdec/common/address.h"

using namespace retdec::common;
using namespace retdec::utils;

namespace retdec {
namespace bin2llvmir {

//
//==============================================================================
// Miscellaneous functions.
//==============================================================================
//

/**
 * We need special function for @c Module printing because
 * its @c print method takes one more parameter.
 * @param t Module to print.
 * @return String with printed module.
 */
std::string llvmObjToString(const llvm::Module* t)
{
	std::string str;
	llvm::raw_string_ostream ss(str);
	if (t)
		t->print(ss, nullptr);
	else
		ss << "nullptr";
	return ss.str();
}

//
//==============================================================================
// Module to LLMV IR file serialization.
//==============================================================================
//

void dumpModuleToFile(
		const llvm::Module* m,
		fs::path dirName,
		const std::string& fileName)
{
	static unsigned cntr = 0;
	std::string n = fileName.empty()
			? "dump_" + std::to_string(cntr++) + ".ll"
			: fileName;

	dirName.append(n);

	std::ofstream myfile(dirName.string());
	myfile << llvmObjToString(m) << std::endl;
}

//
//==============================================================================
// Control flow to JSON serialization.
//==============================================================================
//

namespace {

std::string genIndent(unsigned level = 1, unsigned perLevel = 4, char c = ' ')
{
	std::string indent(perLevel, c);
	std::string ret;
	for (unsigned i = 0; i < level; ++i)
	{
		ret += indent;
	}
	return ret;
}

std::string genJsonLine(const std::string& name, const std::string& val)
{
	return "\"" + name + "\": " + "\"" + val + "\",";
}

void dumpControFlowToJsonBasicBlock(
		llvm::BasicBlock& bb,
		llvm::BasicBlock& bbEnd,
		std::ostream &out)
{
	auto* config = ConfigProvider::getConfig(bb.getModule());

	auto start = AsmInstruction::getTrueBasicBlockAddress(&bb);
	auto end = AsmInstruction::getBasicBlockEndAddress(&bbEnd);

	out << genIndent(3) << "{" << "\n";
	out << genIndent(4) << genJsonLine("address", start.toHexPrefixString()) << "\n";
	out << genIndent(4) << genJsonLine("address_end", end.toHexPrefixString()) << "\n";

	std::set<Address> predsAddrs; // sort addresses
	for (auto pit = pred_begin(&bb), e = pred_end(&bb); pit != e; ++pit)
	{
		// Find BB with address - there should always be some.
		// Some BBs may not have addresses - e.g. those inside
		// if-then-else instruction models.
		auto* pred = *pit;
		auto start = AsmInstruction::getTrueBasicBlockAddress(pred);
		while (start.isUndefined())
		{
			pred = pred->getPrevNode();
			assert(pred);
			start = AsmInstruction::getTrueBasicBlockAddress(pred);
		}
		predsAddrs.insert(start);
	}

	if (predsAddrs.empty())
	{
		out << genIndent(4) << "\"preds\": []," << "\n";
	}
	else
	{
		bool first = true;
		out << genIndent(4) << "\"preds\": [" << "\n";
		for (auto pred : predsAddrs)
		{
			if (first)
			{
				first = false;
			}
			else
			{
				out << ",\n";
			}
			out << genIndent(5) << "\"" << pred.toHexPrefixString() << "\"";
		}
		out << "\n";
		out << genIndent(4) << "]," << "\n";
	}

	std::set<Address> succsAddrs; // sort addresses
	for (auto sit = succ_begin(&bbEnd), e = succ_end(&bbEnd); sit != e; ++sit)
	{
		// Find BB with address - there should always be some.
		// Some BBs may not have addresses - e.g. those inside
		// if-then-else instruction models.
		auto* succ = *sit;
		auto start = AsmInstruction::getTrueBasicBlockAddress(succ);
		while (start.isUndefined())
		{
			succ = succ->getPrevNode();
			assert(succ);
			start = AsmInstruction::getTrueBasicBlockAddress(succ);
		}
		succsAddrs.insert(start);
	}
	// MIPS likely delays slot hack - recognize generated pattern and
	// find all sucessors.
	// Also applicable to ARM cond call/return patterns, and other cases.
	if (config
			&& AsmInstruction::getTrueBasicBlockAddress(&bbEnd).isUndefined() // no addr
			&& (++pred_begin(&bbEnd)) == pred_end(&bbEnd) // single pred
			&& bbEnd.getPrevNode() == *pred_begin(&bbEnd)) // pred right before
	{
		auto* br = llvm::dyn_cast<llvm::BranchInst>((*pred_begin(&bbEnd))->getTerminator());
		if (br
				&& br->isConditional()
				&& br->getSuccessor(0) == &bbEnd
				&& AsmInstruction::getTrueBasicBlockAddress(br->getSuccessor(1)))
		{
			succsAddrs.insert(AsmInstruction::getTrueBasicBlockAddress(br->getSuccessor(1)));
		}
	}

	if (succsAddrs.empty())
	{
		out << genIndent(4) << "\"succs\": []" << "\n";
	}
	else
	{
		bool first = true;
		out << genIndent(4) << "\"succs\": [" << "\n";
		for (auto succ : succsAddrs)
		{
			if (first)
			{
				first = false;
			}
			else
			{
				out << ",\n";
			}
			out << genIndent(5) << "\"" << succ.toHexPrefixString() << "\"";
		}
		out << "\n";
		out << genIndent(4) << "]" << "\n";
	}

	out << genIndent(3) << "}";
}

void dumpControFlowToJsonFunction(
		llvm::Function& f,
		std::ostream &out)
{
	auto* config = ConfigProvider::getConfig(f.getParent());

	auto start = AsmInstruction::getFunctionAddress(&f);
	auto end = AsmInstruction::getFunctionEndAddress(&f);

	out << genIndent(1) << "{\n";
	out << genIndent(2) << genJsonLine("address", start.toHexPrefixString()) << "\n";
	out << genIndent(2) << genJsonLine("address_end", end.toHexPrefixString()) << "\n";
	if (f.empty())
	{
		out << genIndent(2) << "\"bbs\": []" << "\n";
	}
	else
	{
		out << genIndent(2) << "\"bbs\": [" << "\n";

		bool first = true;
		for (llvm::BasicBlock& bb : f)
		{
			// There are more BBs in LLVM IR than we created in control-flow
			// decoding - e.g. BBs inside instructions that behave like
			// if-then-else created by capstone2llvmir.
			if (AsmInstruction::getTrueBasicBlockAddress(&bb).isUndefined())
			{
				continue;
			}

			if (first)
			{
				first = false;
			}
			else
			{
				out << ",\n";
			}

			llvm::BasicBlock* bbEnd = &bb;
			while (bbEnd->getNextNode())
			{
				// Next has address -- is a proper BB.
				//
				if (AsmInstruction::getTrueBasicBlockAddress(bbEnd->getNextNode()).isDefined())
				{
					break;
				}
				else
				{
					bbEnd = bbEnd->getNextNode();
				}
			}

			dumpControFlowToJsonBasicBlock(bb, *bbEnd, out);
		}
		out << "\n";

		out << genIndent(2) << "]," << "\n";
	}

	std::set<Address> usersAddrs; // sort addresses
	for (auto* u : f.users())
	{
		if (auto* i = llvm::dyn_cast<llvm::Instruction>(u))
		{
			if (auto ai = AsmInstruction(i))
			{
				auto addr = ai.getAddress();
				// MIPS hack: there are delay slots on MIPS, calls/branches
				// are placed at the end of the next instruction (delay slot)
				// we need to modify reference address.
				// This assums that all references on MIPS have delays slots of
				// 4 bytes, and therefore need to be fixed, it it is not the
				// case, it will cause problems.
				//
				if (config && config->getConfig().architecture.isMipsOrPic32())
				{
					addr -= 4;
				}
				usersAddrs.insert(addr);
			}
		}
	}

	if (usersAddrs.empty())
	{
		out << genIndent(2) << "\"code_refs\": []" << "\n";
	}
	else
	{
		out << genIndent(2) << "\"code_refs\": [" << "\n";

		bool first = true;
		for (auto& r : usersAddrs)
		{
			if (first)
			{
				first = false;
			}
			else
			{
				out << ",\n";
			}
			out << genIndent(3) << "\"" << r.toHexPrefixString() << "\"";
		}
		out << "\n";

		out << genIndent(2) << "]" << "\n";
	}

	out << genIndent(1) << "}";
}

} // anonymous namespace

void dumpControFlowToJson(
		llvm::Module* m,
		fs::path dirName,
		const std::string& fileName)
{
	dirName.append(fileName);

	std::ofstream json(dirName.string());
	if (!json.is_open())
	{
		return;
	}

	json << "[\n";

	bool first = true;
	for (llvm::Function& f : m->functions())
	{
		if (f.isDeclaration())
		{
			continue;
		}

		// There are some temp and utility fncs that do not have addresses.
		if (AsmInstruction::getFunctionAddress(&f).isUndefined())
		{
			continue;
		}

		if (first)
		{
			first = false;
		}
		else
		{
			json << ",\n";
		}

		dumpControFlowToJsonFunction(f, json);
	}
	json << "\n";

	json << "]\n";
}

} // namespace bin2llvmir
} // namespace retdec
