/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "android-base/logging.h"
#include "dex_builder.h"

#include <fstream>
#include <string>

// Adding tests here requires changes in several other places. See README.md in
// the view_compiler directory for more information.

using namespace startop::dex;
using namespace std;

void GenerateTrivialDexFile(const string& outdir) {
  DexBuilder dex_file;

  ClassBuilder cbuilder{dex_file.MakeClass("android.startop.test.testcases.Trivial")};
  cbuilder.set_source_file("dex_testcase_generator.cc#GenerateTrivialDexFile");

  slicer::MemView image{dex_file.CreateImage()};
  std::ofstream out_file(outdir + "/trivial.dex");
  out_file.write(image.ptr<const char>(), image.size());
}

// Generates test cases that test around 1 instruction.
void GenerateSimpleTestCases(const string& outdir) {
  DexBuilder dex_file;

  ClassBuilder cbuilder{dex_file.MakeClass("android.startop.test.testcases.SimpleTests")};
  cbuilder.set_source_file("dex_testcase_generator.cc#GenerateSimpleTestCases");

  // int return5() { return 5; }
  auto return5{cbuilder.CreateMethod("return5", Prototype{TypeDescriptor::Int()})};
  {
    Value r{return5.MakeRegister()};
    return5.BuildConst4(r, 5);
    return5.BuildReturn(r);
  }
  return5.Encode();

  // // int returnParam(int x) { return x; }
  auto returnParam{cbuilder.CreateMethod("returnParam",
                                         Prototype{TypeDescriptor::Int(), TypeDescriptor::Int()})};
  returnParam.BuildReturn(Value::Parameter(0));
  returnParam.Encode();

  // int returnStringLength(String x) { return x.length(); }
  auto string_type{TypeDescriptor::FromClassname("java.lang.String")};
  MethodDeclData string_length{
      dex_file.GetOrDeclareMethod(string_type, "length", Prototype{TypeDescriptor::Int()})};

  auto returnStringLength{
      cbuilder.CreateMethod("returnStringLength", Prototype{TypeDescriptor::Int(), string_type})};
  {
    Value result = returnStringLength.MakeRegister();
    returnStringLength.AddInstruction(
        Instruction::InvokeVirtual(string_length.id, result, Value::Parameter(0)));
    returnStringLength.BuildReturn(result);
  }
  returnStringLength.Encode();

  // int returnIfZero(int x) { if (x == 0) { return 5; } else { return 3; } }
  MethodBuilder returnIfZero{cbuilder.CreateMethod(
      "returnIfZero", Prototype{TypeDescriptor::Int(), TypeDescriptor::Int()})};
  {
    Value resultIfZero{returnIfZero.MakeRegister()};
    Value else_target{returnIfZero.MakeLabel()};
    returnIfZero.AddInstruction(Instruction::OpWithArgs(
        Instruction::Op::kBranchEqz, /*dest=*/{}, Value::Parameter(0), else_target));
    // else branch
    returnIfZero.BuildConst4(resultIfZero, 3);
    returnIfZero.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturn, /*dest=*/{}, resultIfZero));
    // then branch
    returnIfZero.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBindLabel, /*dest=*/{}, else_target));
    returnIfZero.BuildConst4(resultIfZero, 5);
    returnIfZero.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturn, /*dest=*/{}, resultIfZero));
  }
  returnIfZero.Encode();

  // Make sure backwards branches work too.
  //
  // Pseudo code for test:
  // {
  //   zero = 0;
  //   result = 1;
  //   if (zero == 0) goto B;
  // A:
  //   return result;
  // B:
  //   result = 2;
  //   if (zero == 0) goto A;
  //   result = 3;
  //   return result;
  // }
  // If it runs correctly, this test should return 2.
  MethodBuilder backwardsBranch{
      cbuilder.CreateMethod("backwardsBranch", Prototype{TypeDescriptor::Int()})};
  [](MethodBuilder& method) {
    Value zero = method.MakeRegister();
    Value result = method.MakeRegister();
    Value labelA = method.MakeLabel();
    Value labelB = method.MakeLabel();
    method.BuildConst4(zero, 0);
    method.BuildConst4(result, 1);
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBranchEqz, /*dest=*/{}, zero, labelB));

    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBindLabel, /*dest=*/{}, labelA));
    method.BuildReturn(result);

    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBindLabel, /*dest=*/{}, labelB));
    method.BuildConst4(result, 2);
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBranchEqz, /*dest=*/{}, zero, labelA));

    method.BuildConst4(result, 3);
    method.BuildReturn(result);
  }(backwardsBranch);
  backwardsBranch.Encode();

  // Test that we can make a null value. Basically:
  //
  // public static String returnNull() { return null; }
  MethodBuilder returnNull{cbuilder.CreateMethod("returnNull", Prototype{string_type})};
  [](MethodBuilder& method) {
    Value zero = method.MakeRegister();
    method.BuildConst4(zero, 0);
    method.BuildReturn(zero, /*is_object=*/true);
  }(returnNull);
  returnNull.Encode();

  // Test that we can make String literals. Basically:
  //
  // public static String makeString() { return "Hello, World!"; }
  MethodBuilder makeString{cbuilder.CreateMethod("makeString", Prototype{string_type})};
  [](MethodBuilder& method) {
    Value string = method.MakeRegister();
    method.BuildConstString(string, "Hello, World!");
    method.BuildReturn(string, /*is_object=*/true);
  }(makeString);
  makeString.Encode();

  // Make sure strings are sorted correctly.
  //
  // int returnStringIfZeroAB(int x) { if (x == 0) { return "a"; } else { return "b"; } }
  MethodBuilder returnStringIfZeroAB{
      cbuilder.CreateMethod("returnStringIfZeroAB", Prototype{string_type, TypeDescriptor::Int()})};
  [&](MethodBuilder& method) {
    Value resultIfZero{method.MakeRegister()};
    Value else_target{method.MakeLabel()};
    method.AddInstruction(Instruction::OpWithArgs(
        Instruction::Op::kBranchEqz, /*dest=*/{}, Value::Parameter(0), else_target));
    // else branch
    method.BuildConstString(resultIfZero, "b");
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturnObject, /*dest=*/{}, resultIfZero));
    // then branch
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBindLabel, /*dest=*/{}, else_target));
    method.BuildConstString(resultIfZero, "a");
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturnObject, /*dest=*/{}, resultIfZero));
    method.Encode();
  }(returnStringIfZeroAB);
  // int returnStringIfZeroAB(int x) { if (x == 0) { return "b"; } else { return "a"; } }
  MethodBuilder returnStringIfZeroBA{
      cbuilder.CreateMethod("returnStringIfZeroBA", Prototype{string_type, TypeDescriptor::Int()})};
  [&](MethodBuilder& method) {
    Value resultIfZero{method.MakeRegister()};
    Value else_target{method.MakeLabel()};
    method.AddInstruction(Instruction::OpWithArgs(
        Instruction::Op::kBranchEqz, /*dest=*/{}, Value::Parameter(0), else_target));
    // else branch
    method.BuildConstString(resultIfZero, "a");
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturnObject, /*dest=*/{}, resultIfZero));
    // then branch
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kBindLabel, /*dest=*/{}, else_target));
    method.BuildConstString(resultIfZero, "b");
    method.AddInstruction(
        Instruction::OpWithArgs(Instruction::Op::kReturnObject, /*dest=*/{}, resultIfZero));
    method.Encode();
  }(returnStringIfZeroBA);

  slicer::MemView image{dex_file.CreateImage()};
  std::ofstream out_file(outdir + "/simple.dex");
  out_file.write(image.ptr<const char>(), image.size());
}

int main(int argc, char** argv) {
  CHECK_EQ(argc, 2);

  string outdir = argv[1];

  GenerateTrivialDexFile(outdir);
  GenerateSimpleTestCases(outdir);
}
