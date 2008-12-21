# -*-perl-*-
# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl PSPP.t'

#########################

# change 'tests => 1' to 'tests => last_test_to_print';

use Test::More tests => 19;
use Text::Diff;
use File::Temp qw/ tempfile tempdir /;
BEGIN { use_ok('PSPP') };

#########################

sub run_pspp_syntax
{
    my $tempdir = shift;
    my $syntax = shift;
    my $result = shift;
    my $syntaxfile = "$tempdir/foo.sps";

    open (FH, ">$syntaxfile");
    print FH "$syntax";
    close (FH);

    system ("cd $tempdir; pspp -o raw-ascii $syntaxfile");

    my $diff =  diff ("$tempdir/pspp.list", \$result);

    if ( ! ($diff eq ""))
    {
	diag ("$diff");
    }

    return ($diff eq "");
}


# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

{
  my $d = PSPP::Dict->new();
  ok (ref $d, "Dictionary Creation");

  $d->set_label ("My Dictionary");
  $d->set_documents ("These Documents");

  # Tests for variable creation

  my $var0 = PSPP::Var->new ($d, "le");
  ok (!ref $var0, "Trap illegal variable name");

  $var0 = PSPP::Var->new ($d, "legal");
  ok (ref $var0, "Accept legal variable name");

  my $var1 = PSPP::Var->new ($d, "legal");
  ok (!ref $var1, "Trap duplicate variable name");

  $var1 = PSPP::Var->new ($d, "money", 
			  (fmt=>PSPP::Fmt::DOLLAR, 
			   width=>4, decimals=>2) );
  ok (ref $var1, "Accept valid format");

  $d->set_weight ($var1);


  # Tests for system file creation
  # Make sure a system file can be created
  {
      my $tempdir = tempdir( CLEANUP => 1 );
      my $tempfile = "$tempdir/testfile.sav";
      my $syntaxfile = "$tempdir/syntax.sps";
      my $sysfile = PSPP::Sysfile->new ("$tempfile", $d);
      ok (ref $sysfile, "Create sysfile object");

      $sysfile->close ();
      ok (-s "$tempfile", "Write system file");
  }
}


# Make sure we can write cases to a file
{
  my $d = PSPP::Dict->new();
  PSPP::Var->new ($d, "id",
			 (
			  fmt=>PSPP::Fmt::F, 
			  width=>2, 
			  decimals=>0
			  )
			 );

  PSPP::Var->new ($d, "name",
			 (
			  fmt=>PSPP::Fmt::A, 
			  width=>20, 
			  )
			 );

  $d->set_documents ("This should not appear");
  $d->clear_documents ();
  $d->add_document ("This is a document line");

  $d->set_label ("This is the file label");

  # Check that we can write system files
  {
      my $tempdir = tempdir( CLEANUP => 1 );
      my $tempfile = "$tempdir/testfile.sav";
      my $sysfile = PSPP::Sysfile->new ("$tempfile", $d);

      my $res = $sysfile->append_case ( [34, "frederick"]);
      ok ($res, "Append Case");

      $res = $sysfile->append_case ( [34, "frederick", "extra"]);
      ok (!$res, "Appending Case with too many variables");

      $sysfile->close ();
      ok (-s  "$tempfile", "existance");
  }

  # Check that sysfiles are closed properly
  {
      my $tempdir = tempdir( CLEANUP => 1 );
      my $tempfile = "$tempdir/testfile.sav";
      {
	  my $sysfile = PSPP::Sysfile->new ("$tempfile", $d);

	  my $res = $sysfile->append_case ( [21, "wheelbarrow"]);
	  ok ($res, "Append Case 2");

	  # Don't close.  We want to test that the destructor  does that 
	  # automatically 
      }
      ok (-s "$tempfile", "existance2");

    ok (run_pspp_syntax ($tempdir, <<SYNTAX, <<RESULT), "Check output");

        GET FILE='$tempfile'.
	DISPLAY DICTIONARY.
	DISPLAY FILE LABEL.
	DISPLAY DOCUMENTS.
	LIST.
SYNTAX
1.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|id      |Format: F2.0                               |       1|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|name    |Format: A20                                |       2|
|        |Measure: Nominal                           |        |
|        |Display Alignment: Left                    |        |
|        |Display Width: 20                          |        |
+--------+-------------------------------------------+--------+

File label:
This is the file label

Documents in the active file:

This is a document line

id                 name
-- --------------------
21 wheelbarrow          

RESULT


  }

  # Now do some tests to make sure all the variable parameters 
  # can be written properly.

  {
      my $tempdir = tempdir( CLEANUP => 1 );
      my $tempfile = "$tempdir/testfile.sav";      
      my $dict = PSPP::Dict->new();
      ok (ref $dict, "Dictionary Creation 2");

      my $int = PSPP::Var->new ($dict, "integer", 
				(width=>8, decimals=>0) );

      $int->set_label ("My Integer");
      
      $int->add_value_label (99, "Silly");
      $int->clear_value_labels ();
      $int->add_value_label (0, "Zero");
      $int->add_value_label (1, "Unity");
      $int->add_value_label (2, "Duality");

      my $str = PSPP::Var->new ($dict, "string", 
				(fmt=>PSPP::Fmt::A, width=>8) );


      $str->set_label ("My String");
      ok ($str->add_value_label ("xx", "foo"), "Value label for short string");
      diag ($PSPP::errstr);
      $str->add_value_label ("yy", "bar");

      $str->set_missing_values ("this", "that");

      my $longstr = PSPP::Var->new ($dict, "longstring", 
 				(fmt=>PSPP::Fmt::A, width=>9) );


      $longstr->set_label ("My Long String");
      my $re = $longstr->add_value_label ("xxx", "xfoo");
      ok (($re == 0), "Long strings cant have labels");

      ok ($PSPP::errstr eq "Cannot add label to a long string variable", "Error msg");

      $int->set_missing_values (9, 99);

      my $sysfile = PSPP::Sysfile->new ("$tempfile", $dict);


      $sysfile->close ();

      ok (run_pspp_syntax ($tempdir, <<SYNTAX, <<RESULT), "Check output 2");
GET FILE='$tempfile'.
DISPLAY DICTIONARY.
SYNTAX
1.1 DISPLAY.  
+----------+-----------------------------------------+--------+
|Variable  |Description                              |Position|
#==========#=========================================#========#
|integer   |My Integer                               |       1|
|          |Format: F8.0                             |        |
|          |Measure: Scale                           |        |
|          |Display Alignment: Right                 |        |
|          |Display Width: 8                         |        |
|          |Missing Values: 9; 99                    |        |
|          +-----+-----------------------------------+        |
|          |    0|Zero                               |        |
|          |    1|Unity                              |        |
|          |    2|Duality                            |        |
+----------+-----+-----------------------------------+--------+
|string    |My String                                |       2|
|          |Format: A8                               |        |
|          |Measure: Nominal                         |        |
|          |Display Alignment: Left                  |        |
|          |Display Width: 8                         |        |
|          |Missing Values: "this    "; "that    "   |        |
|          +-----+-----------------------------------+        |
|          |   xx|foo                                |        |
|          |   yy|bar                                |        |
+----------+-----+-----------------------------------+--------+
|longstring|My Long String                           |       3|
|          |Format: A9                               |        |
|          |Measure: Nominal                         |        |
|          |Display Alignment: Left                  |        |
|          |Display Width: 9                         |        |
+----------+-----------------------------------------+--------+

RESULT

  }

}
