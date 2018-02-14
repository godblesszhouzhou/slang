#include "Test.h"

#include "compilation/Compilation.h"
#include "parsing/SyntaxTree.h"

TEST_CASE("Explicit import lookup", "[symbols:lookup]") {
    auto tree = SyntaxTree::fromText(R"(
package Foo;
    parameter int x = 4;
endpackage

import Foo::x;
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    const CompilationUnitSymbol* unit = compilation.getRoot().compilationUnits[0];

    LookupResult result;
    unit->lookupName(compilation.parseName("x"), LookupLocation::max, LookupNameKind::Variable, result);

    CHECK(result.wasImported);
    REQUIRE(result.found);
    CHECK(result.found->kind == SymbolKind::Parameter);
    CHECK(result.found->as<ParameterSymbol>().getValue().integer() == 4);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Wildcard import lookup 1", "[symbols:lookup]") {
    auto tree = SyntaxTree::fromText(R"(
package p;
    parameter int x = 4;
endpackage

module top;
    import p::*;

    if (1) begin : gen_b
        // (2) A lookup here returns p::x
        parameter int x = 12;
        // (1) A lookup here returns local x
    end
    int x;  // If we do a lookup at (2), this becomes an error
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    const auto& top = *compilation.getRoot().topInstances[0];
    const auto& gen_b = top.memberAt<GenerateBlockSymbol>(1);
    const auto& param = gen_b.memberAt<ParameterSymbol>(0);
    CHECK(compilation.getSemanticDiagnostics().empty());
    CHECK(param.getValue().integer() == 12);

    // Lookup at (1); should return the local parameter
    LookupResult result;
    gen_b.lookupName(compilation.parseName("x"), LookupLocation::after(param), LookupNameKind::Variable, result);

    const Symbol* symbol = result.found;
    CHECK(!result.wasImported);
    REQUIRE(symbol);
    CHECK(symbol->kind == SymbolKind::Parameter);
    CHECK(symbol == &param);
    CHECK(compilation.getSemanticDiagnostics().empty());

    // Lookup at (2); should return the package parameter
    gen_b.lookupName(compilation.parseName("x"), LookupLocation::before(param), LookupNameKind::Variable, result);
    symbol = result.found;

    CHECK(result.wasImported);
    REQUIRE(symbol);
    REQUIRE(symbol->kind == SymbolKind::Parameter);
    CHECK(symbol->as<ParameterSymbol>().getValue().integer() == 4);
    NO_COMPILATION_ERRORS;
}

TEST_CASE("Wildcard import lookup 2", "[symbols:lookup]") {
    auto tree = SyntaxTree::fromText(R"(
package p;
    parameter int x = 4;
endpackage

module top;
    import p::*;

    if (1) begin : gen_b
        parameter int foo = x;
        parameter int x = 12;
        parameter int bar = x;
    end
    int x;  // Should be an error here
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    const auto& top = *compilation.getRoot().topInstances[0];
    const auto& gen_b = top.memberAt<GenerateBlockSymbol>(1);

    CHECK(gen_b.find<ParameterSymbol>("foo").getValue().integer() == 4);
    CHECK(gen_b.find<ParameterSymbol>("bar").getValue().integer() == 12);

    auto diags = compilation.getAllDiagnostics();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == DiagCode::ImportNameCollision);
    REQUIRE(diags[0].notes.size() == 3);
    CHECK(diags[0].notes[0].code == DiagCode::NoteDeclarationHere);
    CHECK(diags[0].notes[1].code == DiagCode::NoteImportedFrom);
    CHECK(diags[0].notes[2].code == DiagCode::NoteDeclarationHere);
}

TEST_CASE("Package references", "[symbols:lookup]") {
    auto tree = SyntaxTree::fromText(R"(
package ComplexPkg;
    typedef struct {shortreal i, r;} Complex;

    typedef enum { FALSE, TRUE } bool_t;
endpackage

module top;
    parameter ComplexPkg::Complex blah1 = '0;

    parameter Complex blah2 = '0; // causes an error

    import ComplexPkg::Complex;
    parameter Complex blah3 = '0; // no error

    // Importing an enum type doesn't import the enumerands
    import ComplexPkg::bool_t;
    bool_t b;
    initial begin
        b = ComplexPkg::   FALSE; // ok
        b = FALSE; // error
    end

    initial begin
        // TODO: import ComplexPkg::FALSE;
        b = FALSE;
    end

endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    // Diagnostics diags = compilation.getAllDiagnostics();
    // WARN(DiagnosticWriter{*compilation.getSourceManager()}.report(diags));
    // REQUIRE(diags.size() == 2);
    // CHECK(diags[0].code == DiagCode::UndeclaredIdentifier);
    // CHECK(diags[1].code == DiagCode::UndeclaredIdentifier);
}
