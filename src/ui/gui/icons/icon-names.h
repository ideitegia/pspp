#ifndef ICON_NAMES_H
#define ICON_NAMES_H 1

struct icon_context
{
  const char **icon_name;
  int n_icons;
  const char *context_name;
};


extern const struct icon_context action_icon_context;
extern const struct icon_context category_icon_context;



#endif
