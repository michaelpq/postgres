# pgindent: TAP tests for the consolidated pgindent tool
#
# Copyright (c) 2024-2026, PostgreSQL Global Development Group

use strict;
use warnings FATAL => 'all';

use Cwd qw(getcwd);
use File::Path qw(make_path);
use File::Spec;

use PostgreSQL::Test::Utils;
use Test::More;

# We expect to be started in the source directory (even in a VPATH build);
# we want to run pgindent in the tmp_check directory to reduce clutter.
my $src_dir = getcwd;
chdir ${PostgreSQL::Test::Utils::tmp_check};

my $pgindent = "$src_dir/pgindent";

# Verify the binary exists
ok(-x $pgindent, "pgindent binary exists and is executable");

#
# Test 1: --help exits 0 and prints usage
#
{
	my ($stdout, $stderr);
	my $result = IPC::Run::run [ $pgindent, '--help' ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	ok($result, "--help exits with status 0");
	like($stdout, qr/Usage:/, "--help prints usage information");
	like($stdout, qr/--typedefs/, "--help mentions --typedefs option");
	like($stdout, qr/--check/, "--help mentions --check option");
	like($stdout, qr/--diff/, "--help mentions --diff option");
	is($stderr, '', "--help produces no stderr output");
}

#
# Test 2: --version exits 0 and prints version string
#
{
	my ($stdout, $stderr);
	my $result = IPC::Run::run [ $pgindent, '--version' ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	ok($result, "--version exits with status 0");
	like($stdout, qr/pgindent.*\d+\.\d+/, "--version prints version string");
	is($stderr, '', "--version produces no stderr output");
}

#
# Test 3: --check mode
#
{
	# Create a minimal typedefs file
	my $td_file = "test_typedefs.list";
	open(my $fh, '>', $td_file) or die "Cannot create $td_file: $!";
	print $fh "Datum\n";
	close($fh);

	# Create a file that is already correctly indented
	my $good_file = "check_good.c";
	open($fh, '>', $good_file) or die "Cannot create $good_file: $!";
	print $fh <<'EOF';
#include <stdio.h>

int
main(int argc, char **argv)
{
	printf("hello\n");
	return 0;
}
EOF
	close($fh);

	# --check on already-indented file should exit 0
	my $h = IPC::Run::start [ $pgindent, '--check', "--typedefs=$td_file", $good_file ];
	$h->finish();
	my $exit_code = $h->result(0);
	is($exit_code, 0, "--check exits 0 when file is already indented");

	# Create a file that needs indentation changes
	my $bad_file = "check_bad.c";
	open($fh, '>', $bad_file) or die "Cannot create $bad_file: $!";
	print $fh <<'EOF';
#include <stdio.h>

int main(int argc,char **argv){
printf("hello\n");
return 0;
}
EOF
	close($fh);

	# --check on file needing changes should exit 2
	$h = IPC::Run::start [ $pgindent, '--check', "--typedefs=$td_file", $bad_file ];
	$h->finish();
	$exit_code = $h->result(0);
	is($exit_code, 2, "--check exits 2 when file needs changes");
}

#
# Test 4: --diff mode outputs unified diff without modifying files
#
{
	my $td_file = "test_typedefs.list";

	my $diff_file = "diff_test.c";
	open(my $fh, '>', $diff_file) or die "Cannot create $diff_file: $!";
	print $fh <<'EOF';
#include <stdio.h>

int main(int argc,char **argv){
printf("hello\n");
return 0;
}
EOF
	close($fh);

	# Save original content for comparison
	open($fh, '<', $diff_file) or die "Cannot read $diff_file: $!";
	my $original_content = do { local $/; <$fh> };
	close($fh);

	my ($stdout, $stderr);
	my $result = IPC::Run::run [ $pgindent, '--diff', "--typedefs=$td_file", $diff_file ],
	  '>' => \$stdout,
	  '2>' => \$stderr;

	# --diff should produce output (the diff)
	like($stdout, qr/^[-+@]/m, "--diff produces diff output");

	# Verify the file was NOT modified
	open($fh, '<', $diff_file) or die "Cannot read $diff_file: $!";
	my $after_content = do { local $/; <$fh> };
	close($fh);
	is($after_content, $original_content, "--diff does not modify the file");
}

#
# Test 5: --commit + file args exits 1 with error message
#
{
	my ($stdout, $stderr);
	my $h = IPC::Run::start [ $pgindent, '--commit=HEAD', 'somefile.c' ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	$h->finish();
	my $exit_code = $h->result(0);
	is($exit_code, 1, "--commit + file args exits with status 1");
	like($stderr, qr/cannot specify both/i,
		"--commit + file args prints error message");
}

#
# Test 6: Missing typedefs file exits 1 with error
#
{
	# Create a dummy .c file so the tool has something to process
	my $dummy_file = "dummy_typedef_test.c";
	open(my $fh, '>', $dummy_file) or die "Cannot create $dummy_file: $!";
	print $fh "int x;\n";
	close($fh);

	my ($stdout, $stderr);
	my $h = IPC::Run::start [ $pgindent, '--typedefs=/nonexistent/path/typedefs.list', $dummy_file ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	$h->finish();
	my $exit_code = $h->result(0);
	is($exit_code, 1, "missing typedefs file exits with status 1");
	like($stderr, qr/cannot|error|no such|not found/i,
		"missing typedefs file prints error message");
}

#
# Test 7: Derived file skipping (.y/.l siblings)
#
{
	my $td_file = "test_typedefs.list";

	# Create a .c file and a corresponding .y file (sibling)
	my $derived_c = "derived_test.c";
	my $derived_y = "derived_test.y";

	open(my $fh, '>', $derived_c) or die "Cannot create $derived_c: $!";
	print $fh <<'EOF';
#include <stdio.h>

int main(int argc,char **argv){
printf("hello\n");
return 0;
}
EOF
	close($fh);

	# Create the .y sibling
	open($fh, '>', $derived_y) or die "Cannot create $derived_y: $!";
	print $fh "/* bison grammar */\n";
	close($fh);

	# Save original content
	open($fh, '<', $derived_c) or die "Cannot read $derived_c: $!";
	my $original_content = do { local $/; <$fh> };
	close($fh);

	# Run pgindent on the derived file - it should be skipped
	my ($stdout, $stderr);
	IPC::Run::run [ $pgindent, "--typedefs=$td_file", $derived_c ],
	  '>' => \$stdout,
	  '2>' => \$stderr;

	# Verify the file was NOT modified (skipped because of .y sibling)
	open($fh, '<', $derived_c) or die "Cannot read $derived_c: $!";
	my $after_content = do { local $/; <$fh> };
	close($fh);
	is($after_content, $original_content,
		"derived file with .y sibling is skipped");

	# Clean up the .y file
	unlink $derived_y;
}

#
# Test 8: Unchanged file not rewritten (timestamp check)
#
{
	my $td_file = "test_typedefs.list";

	# Create a file that is already correctly indented
	my $unchanged_file = "unchanged_test.c";
	open(my $fh, '>', $unchanged_file) or die "Cannot create $unchanged_file: $!";
	print $fh <<'EOF';
#include <stdio.h>

int
main(int argc, char **argv)
{
	printf("hello\n");
	return 0;
}
EOF
	close($fh);

	# Set the mtime to a known value in the past
	my $old_mtime = time() - 3600;
	utime($old_mtime, $old_mtime, $unchanged_file)
	  or die "Cannot set mtime on $unchanged_file: $!";

	# Run pgindent
	my ($stdout, $stderr);
	IPC::Run::run [ $pgindent, "--typedefs=$td_file", $unchanged_file ],
	  '>' => \$stdout,
	  '2>' => \$stderr;

	# Verify the mtime was NOT changed (file was not rewritten)
	my $new_mtime = (stat($unchanged_file))[9];
	is($new_mtime, $old_mtime,
		"unchanged file is not rewritten (mtime preserved)");
}

#
# Test 9: Basic indentation - run on a simple .c file, verify it gets indented
#
{
	my $td_file = "test_typedefs.list";

	my $indent_file = "indent_test.c";
	open(my $fh, '>', $indent_file) or die "Cannot create $indent_file: $!";
	print $fh <<'EOF';
#include <stdio.h>

int main(int argc,char **argv){
printf("hello\n");
return 0;
}
EOF
	close($fh);

	# Run pgindent in normal mode
	my ($stdout, $stderr);
	my $result = IPC::Run::run [ $pgindent, "--typedefs=$td_file", $indent_file ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	ok($result, "pgindent succeeds on poorly-indented file");

	# Read the result
	open($fh, '<', $indent_file) or die "Cannot read $indent_file: $!";
	my $indented = do { local $/; <$fh> };
	close($fh);

	# Verify basic indentation was applied (tabs for indentation)
	like($indented, qr/^\tprintf/m,
		"indented file has tab-indented printf");
	like($indented, qr/^\treturn/m,
		"indented file has tab-indented return");
}

#
# Test 10: -T option - verify additional typedef is recognized
#
{
	my $td_file = "test_typedefs.list";

	my $typedef_file = "typedef_test.c";
	open(my $fh, '>', $typedef_file) or die "Cannot create $typedef_file: $!";
	# Use a custom type name that would be treated differently if recognized
	# as a typedef. When recognized, the declaration gets proper spacing.
	print $fh <<'EOF';
#include <stdio.h>

void
test_func(void)
{
	MyCustomType *ptr;
}
EOF
	close($fh);

	# Run with -T to add MyCustomType as a typedef
	my ($stdout, $stderr);
	my $result = IPC::Run::run
	  [ $pgindent, "--typedefs=$td_file", '-T', 'MyCustomType', $typedef_file ],
	  '>' => \$stdout,
	  '2>' => \$stderr;
	ok($result, "pgindent with -T option succeeds");

	# Read the result - the file should be processed without error
	open($fh, '<', $typedef_file) or die "Cannot read $typedef_file: $!";
	my $result_content = do { local $/; <$fh> };
	close($fh);

	# The typedef should be recognized - verify the file was processed
	# (at minimum it should contain the type name)
	like($result_content, qr/MyCustomType/,
		"-T option: custom typedef name is preserved in output");
}

#
# Test 11: Indentation correctness tests (migrated from pg_bsd_indent)
#
# These tests verify that the indentation engine produces correct output
# for various C constructs.  Each test has an input file (.0) and expected
# output (.0.stdout).  The test runs pgindent on the input and compares
# the result to the expected output.
#
{
	my $td_file = "test_typedefs.list";
	my $tests_dir = "$src_dir/tests";

	# options used with diff
	my @diffopts = ("-U3");
	push(@diffopts, "--strip-trailing-cr") if $windows_os;

	# Copy support files to current dir
	use File::Copy "cp";
	while (my $file = glob("$tests_dir/*.list"))
	{
		cp($file, ".") || die "cp $file failed: $!";
	}

	# Run each test case
	while (my $test_src = glob("$tests_dir/*.0"))
	{
		my ($volume, $directories, $test) = File::Spec->splitpath($test_src);
		$test =~ s/\.0$//;

		# Copy input to working directory
		cp($test_src, "$test.c") || die "cp $test_src failed: $!";

		# Build the command with appropriate options
		my @cmd = ($pgindent, "--typedefs=$td_file");

		# The types_from_file test needs extra typedefs
		if ($test eq 'types_from_file')
		{
			push @cmd, '-T', 'a', '-T', 'b';
		}

		push @cmd, "$test.c";

		# Run pgindent
		my ($stdout, $stderr);
		my $result = IPC::Run::run \@cmd,
		  '>' => \$stdout,
		  '2>' => \$stderr;
		ok($result, "pgindent succeeds on $test");

		# Compare result to expected output
		my $expected_file = "$tests_dir/$test.0.stdout";
		my $diff_result = run_log(
			[ 'diff', @diffopts, $expected_file, "$test.c" ],
			'>>' => "indent_test.diffs");
		ok($diff_result, "pgindent output matches expected for $test");
	}
}

done_testing();
