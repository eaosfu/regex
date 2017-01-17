#!/usr/bin/perl

use strict;
use warnings;

use IO::Dir;
use IO::Handle;
use FindBin;
use Term::ANSIColor;
FindBin::again();

# options
my $verbose = 1;

# directories
my $cwd                 = $FindBin::Bin;
my $test_cases_dir      = "$cwd/test_cases";
my $test_input_dir      = "$cwd/test_input";
my $regex_input_dir     = "$cwd/regex_input";
my $regex_output_dir    = "$cwd/regex_output";
my $grep_output_dir     = "$cwd/grep_output";
my $diff_output_dir     = "$cwd/diff";


# file with regex expressions to test
my $combined_input_file = "$test_cases_dir/test_r2.txt";

# executable
my $bin_dir = "$cwd/../bin";
my $regex_bin = "$bin_dir/recognizer";


sub separate_combined_input {
  my ($combined_input, $test_input_dir) = @_;

  open(my $FH, '<', $combined_input)
    or die "Unable to open combined input file: $combined_input: $!\n";

  my $test_num = 0;
  my %tests;

  while(<$FH>) {
    chomp($_);
    if($_ =~ /^grep:\s+(grep\s+[^']+'.*')\s+(.*)/) {
      if($tests{$test_num}{"grep"}) {
        ++$test_num;
      }
      $tests{$test_num}{"grep"} = $1;
      $tests{$test_num}{"grep_input"} = "$test_input_dir/$2";
    }
    if($_ =~ /^regex:\s+'(.*)'\s+(.*)/) {
      if($tests{$test_num}{"regex"}) {
        ++$test_num;
      }
      $tests{$test_num}{"regex"} = $1;
      # The input line for grep already includes the input
      # file to run against, we need it pass it separately
      # to the our regex engine
      $tests{$test_num}{"regex_input"} = "$test_input_dir/$2";
    }
  }

  my @zero_prefix;

  foreach (sort {$a <=> $b} keys %tests) {
    @zero_prefix = ("0" x (length("$test_num") - length("$_")));
    # generate name for the  input/output files
    $tests{$_}{"regex_exp_input"} =
      "$regex_input_dir/regex_"."@zero_prefix"."$_";

    $tests{$_}{"regex_output"} =
      "$regex_output_dir/regex_"."@zero_prefix"."$_";

    $tests{$_}{"grep_output"} =
      "$grep_output_dir/grep_"."@zero_prefix"."$_";

    # The regex engine will read the regex from a file  in:
    #   <tests dir>/regex_input/regex_###
    # The result of the search is written to the file in:
    #   <tests dir>/regex_input/regex_###
    $tests{$_}{"inout_file_suffix"} = "@zero_prefix"."$_";
  }

  return %tests;
}


sub gen_grep_output {
  my $args = shift;
  my ($cmd, $input, $output);
  print "Generating grep output files for comparison\n" if($verbose);
  foreach (sort {$a <=> $b} keys %{$args}) {
    $input  = $args->{$_}{"grep_input"};
    $output = $args->{$_}{"grep_output"};
    $cmd    = "$args->{$_}{grep} $input > $output";
    eval{`$cmd 2> /dev/null`};
    die "Error running grep command: $cmd: $!\n" if($@);
  }

}


sub gen_regex_input {
  # run the regex binary
  my ($input_dir, $args) = @_;
  my ($cmd, $regex, $generated_input_file);
  print "Generating regex input files\n" if($verbose);
  foreach (sort {$a <=> $b} keys %{$args}) {
    $regex = $args->{$_}{"regex"};
    $generated_input_file = "$input_dir/regex_" . $args->{$_}{"inout_file_suffix"};
    $cmd = "echo '$regex' > $generated_input_file";
    eval{`$cmd 2> /dev/null`};
    die "Error running grep command: $cmd: $!\n" if($@);
  }
}


sub gen_regex_output {
  my ($cmd, $exp_file, $target, $out_file);
  my ($regex_bin, $output_dir, $regex_input, $args) = @_;
  my $total = scalar (keys %{$args});
  print "Running the regex executable against $total test cases\n" if($verbose);
  foreach (sort {$a <=> $b} keys %{$args}) {
    $target   = "$args->{$_}{regex_input}";
    $exp_file = "$regex_input/regex_$args->{$_}{inout_file_suffix}";
    $out_file = "$output_dir/regex_$args->{$_}{inout_file_suffix}";
    $cmd      = "$regex_bin $exp_file $target > $out_file";
    eval{`$cmd 2> /dev/null`};
    die "Error running grep command: $cmd: $!\n" if($@);
  }
}


sub check_dirs_and_files {
  my ($test_cases_dir,
      $input_dir,
      $regex_output_dir,
      $regex_input_dir,
      $grep_output_dir,
      $diff_output_dir,
      $test_input_file,
      $regex_bin) = @_;

  die "No 'input dir' provided \n"       if(!defined($test_cases_dir));
  die "No 'test cases dir' provided \n"  if(!defined($input_dir));
  die "No 'regex input dir' provided\n"  if(!defined($regex_input_dir));
  die "No 'regex output dir' provided\n" if(!defined($regex_output_dir));
  die "No 'grep output file' provided\n" if(!defined($grep_output_dir));
  die "No 'regex input file' provided'\n"if(!defined($test_input_file));
  die "No 'executable' provided'\n"      if(!defined($regex_bin));

  # test directories exist, if they don't, create them
  my @dirs = ($test_cases_dir , $input_dir, $regex_output_dir, 
              $grep_output_dir, $diff_output_dir);

  foreach(@dirs) {
    if(! -d $_) {
      mkdir($_) or die "Failed to create directory: $_: $!\n";
    }
  }
}


sub diff_results {
  my $args = shift;
  my ($cmd, $grep_out, $regex_out, $diff_out, $result, $fail_count, $total, $result_msg);
  print color ('red') ;
  foreach (sort {$a <=> $b} keys %{$args}) {
    $regex_out = "$args->{$_}{regex_output}";
    $grep_out  = "$args->{$_}{grep_output}";
    $diff_out  = "$diff_output_dir/test_$args->{$_}{inout_file_suffix}";
    $cmd       = "diff $regex_out $grep_out > $diff_out";

    eval {
      $result = `$cmd 2> /dev/null ; echo \$?`
    }; die "Error running diff command: $cmd: $!\n" if($@);

    if($result != 0) {
      $fail_count++;
      print "Failed test $_: $args->{$_}{regex}\n" if($verbose);
    }
  }
  $total = scalar (keys %{$args});
  print color('reset');
  print "Failed $fail_count/". $total . "\n";
}


sub main {
  my @args = ();

  @args = ($test_input_dir , $test_cases_dir , $regex_output_dir   , $regex_output_dir,
          $grep_output_dir, $diff_output_dir, $combined_input_file,
          $regex_bin);

  check_dirs_and_files(@args);
  @args = ($combined_input_file, $test_input_dir);
  my %input = separate_combined_input($combined_input_file, $test_input_dir);
  gen_regex_input($regex_input_dir, \%input);
  gen_grep_output(\%input);
  gen_regex_output($regex_bin, $regex_output_dir, $regex_input_dir, \%input);
  diff_results(\%input);
}


&main();
