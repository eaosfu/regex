#!/usr/bin/perl

use strict;
use warnings;

my $diff_dir = "./diff";
my $grep_output_dir = "./grep_output";
my $regex_output_dir = "./regex_output";
my $regex_input_dir = "./regex_input";


# check that the requisite directories exist
foreach (("$diff_dir", "$grep_output_dir", "$regex_output_dir", "$regex_input_dir")) {
  if( -d $_ ) {
    continue;
  }
  else {
    mkdir($_) or die "Unable to create dir: $_\n";
  }
}

my $regex = "./test_r2.txt";
my $src = "./test_input";

my $test_num = 0;
my %tests;


open(FH, "<", $regex) or die "Unable to open file $regex: $!\n";

my $inputs = 0;

while(<FH>) {
  chomp($_);
  if($_ =~ /^grep:\s+(.*)/) {
    if($tests{$test_num}{"grep"}) {
      ++$test_num;
    }
    $tests{$test_num}{"grep"} = $1;
  }
  if($_ =~ /^regex:\s+(.*)/) {
    if($tests{$test_num}{"regex"}) {
      ++$test_num;
    }
    $tests{$test_num}{"regex"} = $1;
  }
}


my @args;
my $cmd;
my $input_regex;
my @zero_prefix;
my ($grep_test, $regex_test, $diff_test);

foreach (sort {$a <=> $b} keys %tests) {
  @zero_prefix = ("0" x (length("$test_num") - length("$_")));
  $grep_test = "grep_" . "@zero_prefix" . "$_";
  $regex_test = "regex_" . "@zero_prefix" . "$_";
  $diff_test = "test_" . "@zero_prefix" . "$_";
  @args = ();
  @args = split(' ', $tests{$_}{"grep"});
  push(@args, ">");
  push(@args, "tests/grep_output/$grep_test");
  $cmd = join(" ", @args);
  print "$cmd\n";
  eval{`$cmd`};
# generate input for regex engine
  @args = ();
  $input_regex = "'" . $tests{$_}{"regex"} . "'";
  push(@args, $input_regex);
  unshift(@args, "echo ");
  push(@args, " > ");
  push(@args, "tests/regex_input/$regex_test");
  print "\n";
  $cmd = join(" ", @args);
#  print "$cmd\n";
  eval{`$cmd`};
  eval { `./a.out tests/regex_input/$regex_test $src > tests/regex_output/$regex_test` };
  eval { `valgrind --error-exitcode=1 --log-file=/dev/null ./a.out tests/regex_input/$regex_test $src 2>&1 /dev/null && echo \$? > tests/memcheck/$diff_test` };
  eval { `echo 'regex_engine vs. grep' > tests/diff/$_` };
  eval { `echo $input_regex >> tests/diff/$_` };
  eval { `diff tests/regex_output/$regex_test tests/grep_output/$grep_test && echo >> tests/diff/$diff_test` };
}
