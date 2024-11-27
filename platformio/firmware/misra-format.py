import re


def format_rules(input_file, output_file):
    # Compile a regular expression to match the [rule] [rule text] pattern.
    pattern = re.compile(r"^(Dir|Rule) \d+\.\d+ \w+ ")

    with open(input_file, "r", encoding="utf-8") as infile, open(
        output_file, "w", encoding="utf-8"
    ) as outfile:
        for line in infile:
            line = line.strip()
            match = pattern.match(line)
            if match:
                # Extract the rule part
                rule = line[: match.end()].strip()
                # Extract the rule text part
                rule_text = line[match.end() :].strip()

                # Write to output
                outfile.write(rule + "\n")
                outfile.write(rule_text + "\n")


if __name__ == "__main__":
    input_filename = "misra.txt"
    output_filename = "formatted-misra.txt"
    format_rules(input_filename, output_filename)
    print(f"Formatted rules written to {output_filename}")
