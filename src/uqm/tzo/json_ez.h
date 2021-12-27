#include "thirdparty/json.h"

struct json_value_s *get_object_key_v(struct json_object_s *obj, char *key)
{
  for (struct json_object_element_s *oe = obj->start; oe != NULL; oe = oe->next)
  {
    if (0 == strcmp(oe->name->string, key))
    {
      return oe->value;
    }
  }
  return NULL;
}

struct json_array_s *get_object_key_as_array(struct json_object_s *obj, char *key)
{
  struct json_value_s *o = get_object_key_v(obj, key);
  if (o != NULL)
  {
    return json_value_as_array(o);
  }
  return NULL;
}

struct json_object_s *get_object_key_as_object(struct json_object_s *obj, char *key)
{
  struct json_value_s *o = get_object_key_v(obj, key);
  if (o != NULL)
  {
    return json_value_as_object(o);
  }
  return NULL;
}

struct json_string_s *get_object_key_as_string(struct json_object_s *obj, char *key)
{
  struct json_value_s *o = get_object_key_v(obj, key);
  if (o != NULL)
  {
    return json_value_as_string(o);
  }
  return NULL;
}
