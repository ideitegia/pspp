/* This file can be included multiple times.  It redeclares its macros
   appropriately each time, like assert.h. */

#undef debug_printf
#undef debug_puts
#undef debug_putc

#if DEBUGGING

#define debug_printf(args)			\
	do					\
	  {					\
	    printf args;			\
	    fflush (stdout);			\
	  }					\
	while (0)
	
#define debug_puts(string)			\
	do					\
	  {					\
	    puts (string);			\
	    fflush (stdout);			\
	  }					\
	while (0)

#define debug_putc(char, stream)		\
	do					\
	  {					\
	    putc (char, stream);		\
	    fflush (stdout);			\
	  }					\
	while (0)

#else /* !DEBUGGING */

#define debug_printf(args)			\
	do					\
	  {					\
	  }					\
	while (0)

#define debug_puts(string)			\
	do					\
	  {					\
	  }					\
	while (0)

#define debug_putc(char, stream)		\
	do					\
	  {					\
	  }					\
	while (0)

#endif /* !DEBUGGING */
