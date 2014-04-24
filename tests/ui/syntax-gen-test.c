#include <config.h>

#include "ui/syntax-gen.h"

#include <stdio.h>

static void
test_runner (const char *format, ...)
{
  struct string syntax;
  va_list args;
  va_start (args, format);

  ds_init_empty (&syntax);
  
  syntax_gen_pspp_valist (&syntax, format, args);

  va_end (args);

  puts (ds_cstr (&syntax));

  ds_destroy (&syntax);  
}

int
main (void)
{
  test_runner ("A simple string: %ssEND", "Hello world");
  test_runner ("A syntax string: %sqEND", "Hello world");
  test_runner ("A syntax string containing \": %sqEND", "here\"is the quote");
  test_runner ("A syntax string containing non-printables: %sqEND", "A CtrlLchar");
  test_runner ("An integer: %dEND", 98765);
  test_runner ("A floating point number: %gEND", 3.142);
  test_runner ("A floating point number with default precision: %fEND", 1.234);
  test_runner ("A floating point number with given precision: %.20fEND", 1.234);
  test_runner ("A literal %%");

  test_runner ("and %ss a %sq of %d different %f examples %g of 100%% conversions.",
               "finally", "concatination", 6, 20.309, 23.09);

  return 0;
}
