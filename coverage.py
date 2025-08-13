#!/usr/bin/env python3
"""
C++ Code Coverage Instrumenter
Inserts COVER() calls at function definitions, if statements, and else statements.
"""

import sys
import argparse
from tree_sitter import Language, Parser
import tree_sitter_cpp as tscpp

class CppCoverageInstrumenter:
    def __init__(self, source_code):
        self.source_code = source_code
        self.lines = source_code.split('\n')
        self.counter = 0
        self.coverage_points = []
        self.insertions = []  # List of (line_number, column, text) tuples
        self.current_function = None  # Track current function name

        # Initialize tree-sitter parser
        CPP_LANGUAGE = Language(tscpp.language())
        self.parser = Parser(CPP_LANGUAGE)

    def get_next_id(self):
        self.counter += 1
        prefix = self.current_function if self.current_function else "global"
        return f"{prefix}_{self.counter}"

    def find_insertion_point(self, node):
        """Find the line and column to insert COVER() after the opening brace"""
        text = node.text.decode('utf-8')

        # Find the first '{' in the node
        brace_pos = text.find('{')
        if brace_pos == -1:
            return None

        # Calculate absolute position
        start_byte = node.start_byte + brace_pos + 1

        # Convert byte position to line/column
        current_byte = 0
        for line_num, line in enumerate(self.lines):
            line_bytes = len(line) + 1  # +1 for newline
            if current_byte + line_bytes > start_byte:
                column = start_byte - current_byte
                return (line_num, column)
            current_byte += line_bytes

        return None

    def process_node(self, node, context=""):
        """Recursively process AST nodes"""

        # Function definitions
        if node.type == 'function_definition':
            # Get function name first
            func_name = "unknown"
            for c in node.children:
                if c.type == 'function_declarator':
                    for cc in c.children:
                        if cc.type == 'identifier':
                            func_name = cc.text.decode('utf-8')
                            break

            # Set current function context
            old_function = self.current_function
            self.current_function = func_name

            # Find the compound_statement (function body)
            for child in node.children:
                if child.type == 'compound_statement':
                    insertion_point = self.find_insertion_point(child)
                    if insertion_point:
                        cover_id = self.get_next_id()
                        line_num, col = insertion_point

                        self.coverage_points.append({
                            'id': cover_id,
                            'type': 'function',
                            'name': func_name,
                            'line': line_num + 1
                        })

                        # Check if line after brace is empty or just whitespace
                        if line_num + 1 < len(self.lines):
                            next_line = self.lines[line_num + 1]
                            if next_line.strip() == '':
                                # Insert on the empty line
                                indent = '    '  # Default 4 spaces
                                # Try to detect indentation from next non-empty line
                                for check_line in self.lines[line_num + 2:]:
                                    if check_line.strip():
                                        indent = len(check_line) - len(check_line.lstrip())
                                        indent = check_line[:indent]
                                        break
                                self.insertions.append((line_num + 1, 0, f'{indent}COVER("{cover_id}");'))
                            else:
                                # Insert at the beginning of the next line with proper indentation
                                indent = len(next_line) - len(next_line.lstrip())
                                self.insertions.append((line_num + 1, 0, f'{next_line[:indent]}COVER("{cover_id}");\n'))

            # Process children (including nested functions)
            for child in node.children:
                self.process_node(child)

            # Restore previous function context
            self.current_function = old_function
            return  # Don't process children again

        # If statements
        elif node.type == 'if_statement':
            # Process the 'then' branch (consequence)
            for child in node.children:
                if child.type == 'compound_statement':
                    insertion_point = self.find_insertion_point(child)
                    if insertion_point:
                        cover_id = self.get_next_id()
                        line_num, col = insertion_point

                        self.coverage_points.append({
                            'id': cover_id,
                            'type': 'if_branch',
                            'line': line_num + 1
                        })

                        # Similar insertion logic as functions
                        if line_num + 1 < len(self.lines):
                            next_line = self.lines[line_num + 1]
                            if next_line.strip() == '':
                                indent = '    '
                                for check_line in self.lines[line_num + 2:]:
                                    if check_line.strip():
                                        indent = len(check_line) - len(check_line.lstrip())
                                        indent = check_line[:indent]
                                        break
                                self.insertions.append((line_num + 1, 0, f'{indent}COVER("{cover_id}");'))
                            else:
                                indent = len(next_line) - len(next_line.lstrip())
                                self.insertions.append((line_num + 1, 0, f'{next_line[:indent]}COVER("{cover_id}");\n'))

            # Look for else clause
            else_found = False
            for i, child in enumerate(node.children):
                if child.type == 'else':
                    else_found = True
                elif else_found and child.type == 'compound_statement':
                    insertion_point = self.find_insertion_point(child)
                    if insertion_point:
                        cover_id = self.get_next_id()
                        line_num, col = insertion_point

                        self.coverage_points.append({
                            'id': cover_id,
                            'type': 'else_branch',
                            'line': line_num + 1
                        })

                        if line_num + 1 < len(self.lines):
                            next_line = self.lines[line_num + 1]
                            if next_line.strip() == '':
                                indent = '    '
                                for check_line in self.lines[line_num + 2:]:
                                    if check_line.strip():
                                        indent = len(check_line) - len(check_line.lstrip())
                                        indent = check_line[:indent]
                                        break
                                self.insertions.append((line_num + 1, 0, f'{indent}COVER("{cover_id}");'))
                            else:
                                indent = len(next_line) - len(next_line.lstrip())
                                self.insertions.append((line_num + 1, 0, f'{next_line[:indent]}COVER("{cover_id}");\n'))

        # Recursively process children
        for child in node.children:
            self.process_node(child)

    def instrument(self):
        """Parse and instrument the C++ code"""
        tree = self.parser.parse(bytes(self.source_code, 'utf-8'))
        self.process_node(tree.root_node)

        # Sort insertions by line number (reverse order to maintain positions)
        self.insertions.sort(key=lambda x: (x[0], x[1]), reverse=True)

        # Apply insertions to create instrumented code
        instrumented_lines = self.lines.copy()
        for line_num, col, text in self.insertions:
            if col == 0:
                # Insert as new line or at beginning
                if instrumented_lines[line_num].strip() == '':
                    instrumented_lines[line_num] = text
                else:
                    instrumented_lines[line_num] = text + '\n' + instrumented_lines[line_num]
            else:
                # Insert at specific column
                line = instrumented_lines[line_num]
                instrumented_lines[line_num] = line[:col] + text + line[col:]

        return '\n'.join(instrumented_lines)

    def generate_coverage_header(self):
        """Generate the coverage tracking code to be included"""
        header = """// ===== COVERAGE TRACKING CODE =====
#include <iostream>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <vector>

// Global set of uncovered points - starts with all points
std::unordered_set<std::string> __uncovered_points = {"""

        # Add all coverage points
        points_list = ', '.join([f'"{p["id"]}"' for p in self.coverage_points])
        header += points_list

        header += """};

// Total number of coverage points
const size_t __total_points = """ + str(len(self.coverage_points)) + """;

// Function to mark coverage - removes from uncovered set
void COVER(const std::string& point) {
    __uncovered_points.erase(point);
}

// Function to print coverage report
void print_coverage_report() {
    size_t covered_count = __total_points - __uncovered_points.size();

    std::cout << "\\n===== COVERAGE REPORT =====\\n";
    std::cout << "Total coverage points: " << __total_points << "\\n";
    std::cout << "Points covered: " << covered_count << "\\n";
    std::cout << "Coverage: " << (100.0 * covered_count / __total_points) << "%\\n\\n";

    if (__uncovered_points.empty()) {
        std::cout << "✓ All paths covered!\\n";
    } else {
        std::cout << "Uncovered points:\\n";
        for (const auto& point : __uncovered_points) {
            std::cout << "  ✗ " << point << "\\n";
        }
    }
}

// Automatically print report at program exit
struct CoverageReporter {
    ~CoverageReporter() {
        print_coverage_report();
    }
};
CoverageReporter __coverage_reporter;

// ===== END COVERAGE TRACKING =====

"""
        return header

def main():
    parser = argparse.ArgumentParser(description='Instrument C++ code for coverage tracking')
    parser.add_argument('input_file', help='Input C++ file')
    parser.add_argument('-o', '--output', help='Output file (default: <input>_instrumented.cpp)')
    parser.add_argument('--header-only', action='store_true', help='Only output the coverage header')

    args = parser.parse_args()

    # Read input file
    try:
        with open(args.input_file, 'r') as f:
            source_code = f.read()
    except FileNotFoundError:
        print(f"Error: File '{args.input_file}' not found")
        sys.exit(1)

    # Instrument the code
    instrumenter = CppCoverageInstrumenter(source_code)
    instrumented_code = instrumenter.instrument()

    # Generate output
    if args.header_only:
        output = instrumenter.generate_coverage_header()
    else:
        output = instrumenter.generate_coverage_header() + instrumented_code

    # Write output
    if args.output:
        output_file = args.output
    else:
        output_file = args.input_file.replace('.cpp', '_instrumented.cpp')

    with open(output_file, 'w') as f:
        f.write(output)

    print(f"Instrumented code written to: {output_file}")
    print(f"Total coverage points added: {instrumenter.counter}")
    print("\nCoverage points:")
    for point in instrumenter.coverage_points:
        print(f"  {point['id']}: {point['type']} at line {point['line']}")

if __name__ == '__main__':
    main()
