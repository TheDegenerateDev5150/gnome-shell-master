/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-theme.c: A set of CSS stylesheets used for rule matching
 *
 * Copyright 2003-2004 Dodji Seketeli
 * Copyright 2008, 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * This file started as a cut-and-paste of cr-sel-eng.c from libcroco.
 *
 * In moving it to hippo-canvas:
 * - Reformatted and otherwise edited to match our coding style
 * - Switched from handling xmlNode to handling HippoStyle
 * - Simplified by removing things that we don't need or that don't
 *   make sense in our context.
 * - The code to get a list of matching properties works quite differently;
 *   we order things in priority order, but we don't actually try to
 *   coalesce properties with the same name.
 *
 * In moving it to GNOME Shell:
 *  - Renamed again to StTheme
 *  - Reformatted to match the gnome-shell coding style
 *  - Removed notion of "theme engine" from hippo-canvas
 *  - pseudo-class matching changed from link enum to strings
 *  - Some code simplification
 */


#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#include "st-private.h"
#include "st-theme-node.h"
#include "st-theme-private.h"

static void st_theme_constructed  (GObject      *object);
static void st_theme_finalize     (GObject      *object);
static void st_theme_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec);
static void st_theme_get_property (GObject      *object,
                                   guint         prop_id,
                                   GValue       *value,
                                   GParamSpec   *pspec);

struct _StTheme
{
  GObject parent;

  GFile *application_stylesheet;
  GFile *default_stylesheet;
  GFile *theme_stylesheet;
  GSList *custom_stylesheets;

  GHashTable *stylesheets_by_file;
  GHashTable *files_by_stylesheet;

  CRCascade *cascade;
};

enum
{
  PROP_0,
  PROP_APPLICATION_STYLESHEET,
  PROP_THEME_STYLESHEET,
  PROP_DEFAULT_STYLESHEET
};

enum
{
  STYLESHEETS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (StTheme, st_theme, G_TYPE_OBJECT)

/* Quick strcmp.  Test only for == 0 or != 0, not < 0 or > 0.  */
#define strqcmp(str,lit,lit_len) \
  (strlen (str) != (lit_len) || memcmp (str, lit, lit_len))

static gboolean
file_equal0 (GFile *file1,
             GFile *file2)
{
  if (file1 == file2)
    return TRUE;

  if ((file1 == NULL) || (file2 == NULL))
    return FALSE;

  return g_file_equal (file1, file2);
}

static void
st_theme_init (StTheme *theme)
{
  theme->stylesheets_by_file = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal,
                                                      (GDestroyNotify)g_object_unref, (GDestroyNotify)cr_stylesheet_unref);
  theme->files_by_stylesheet = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
st_theme_class_init (StThemeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = st_theme_constructed;
  object_class->finalize = st_theme_finalize;
  object_class->set_property = st_theme_set_property;
  object_class->get_property = st_theme_get_property;

  /**
   * StTheme:application-stylesheet:
   *
   * The highest priority stylesheet, representing application-specific
   * styling; this is associated with the CSS "author" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_APPLICATION_STYLESHEET,
                                   g_param_spec_object ("application-stylesheet", NULL, NULL,
                                                        G_TYPE_FILE,
                                                        ST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * StTheme:theme-stylesheet:
   *
   * The second priority stylesheet, representing theme-specific styling;
   * this is associated with the CSS "user" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_THEME_STYLESHEET,
                                   g_param_spec_object ("theme-stylesheet", NULL, NULL,
                                                        G_TYPE_FILE,
                                                        ST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * StTheme:default-stylesheet:
   *
   * The lowest priority stylesheet, representing global default
   * styling; this is associated with the CSS "user agent" stylesheet.
   */
  g_object_class_install_property (object_class,
                                   PROP_DEFAULT_STYLESHEET,
                                   g_param_spec_object ("default-stylesheet", NULL, NULL,
                                                        G_TYPE_FILE,
                                                        ST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  signals[STYLESHEETS_CHANGED] =
    g_signal_new ("custom-stylesheets-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, /* no default handler slot */
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static CRStyleSheet *
parse_stylesheet (GFile   *file,
                  GError **error)
{
  enum CRStatus status;
  CRStyleSheet *stylesheet;
  char *contents;
  gsize length;

  if (file == NULL)
    return NULL;

  if (!g_file_load_contents (file, NULL, &contents, &length, NULL, error))
    return NULL;

  status = cr_om_parser_simply_parse_buf ((const guchar *) contents,
                                          length,
                                          CR_UTF_8,
                                          &stylesheet);
  g_free (contents);

  if (status != CR_OK)
    {
      char *uri = g_file_get_uri (file);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Error parsing stylesheet '%s'; errcode:%d", uri, status);
      g_free (uri);
      return NULL;
    }

  /* Extension stylesheet */
  stylesheet->app_data = GUINT_TO_POINTER (FALSE);

  return stylesheet;
}

CRDeclaration *
_st_theme_parse_declaration_list (const char *str)
{
  return cr_declaration_parse_list_from_buf ((const guchar *)str,
                                             CR_UTF_8);
}

/* Just g_warning for now until we have something nicer to do */
static CRStyleSheet *
parse_stylesheet_nofail (GFile *file)
{
  GError *error = NULL;
  CRStyleSheet *result;

  result = parse_stylesheet (file, &error);
  if (error)
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
    }
  return result;
}

static void
insert_stylesheet (StTheme      *theme,
                   GFile        *file,
                   CRStyleSheet *stylesheet)
{
  if (stylesheet == NULL)
    return;

  g_object_ref (file);
  cr_stylesheet_ref (stylesheet);

  g_hash_table_insert (theme->stylesheets_by_file, file, stylesheet);
  g_hash_table_insert (theme->files_by_stylesheet, stylesheet, file);
}

static CRStyleSheet *
resolve_stylesheet (StTheme  *theme,
                    GFile    *file,
                    GError  **error)
{
  CRStyleSheet *sheet;

  sheet = g_hash_table_lookup (theme->stylesheets_by_file, file);
  if (sheet)
    {
      cr_stylesheet_ref (sheet);
      return sheet;
    }

  sheet = parse_stylesheet (file, error);
  if (sheet)
    insert_stylesheet (theme, file, sheet);

  return sheet;
}

/**
 * st_theme_load_stylesheet:
 * @theme: a #StTheme
 * @file: a #GFile
 * @error: a #GError
 *
 * Load the stylesheet associated with @file.
 *
 * Returns: %TRUE if successful
 */
gboolean
st_theme_load_stylesheet (StTheme    *theme,
                          GFile      *file,
                          GError    **error)
{
  CRStyleSheet *stylesheet;

  stylesheet = resolve_stylesheet (theme, file, error);
  if (!stylesheet)
    return FALSE;

  stylesheet->app_data = GUINT_TO_POINTER (TRUE);

  cr_stylesheet_ref (stylesheet);
  theme->custom_stylesheets = g_slist_prepend (theme->custom_stylesheets, stylesheet);
  g_signal_emit (theme, signals[STYLESHEETS_CHANGED], 0);

  return TRUE;
}

/**
 * st_theme_unload_stylesheet:
 * @theme: a #StTheme
 * @file: a #GFile
 *
 * Unload the stylesheet associated with @file. If @file was not loaded this
 * function does nothing.
 */
void
st_theme_unload_stylesheet (StTheme    *theme,
                            GFile      *file)
{
  CRStyleSheet *stylesheet;

  stylesheet = g_hash_table_lookup (theme->stylesheets_by_file, file);
  if (!stylesheet)
    return;

  if (!g_slist_find (theme->custom_stylesheets, stylesheet))
    return;

  theme->custom_stylesheets = g_slist_remove (theme->custom_stylesheets, stylesheet);

  g_signal_emit (theme, signals[STYLESHEETS_CHANGED], 0);

  /* We need to remove the entry from the hashtable after emitting the signal
   * since we might still access the files_by_stylesheet hashtable in
   * _st_theme_resolve_url() during the signal emission.
   */
  g_hash_table_remove (theme->stylesheets_by_file, file);
  g_hash_table_remove (theme->files_by_stylesheet, stylesheet);
  cr_stylesheet_unref (stylesheet);
}

/**
 * st_theme_get_custom_stylesheets:
 * @theme: an #StTheme
 *
 * Get a list of the stylesheet files loaded with st_theme_load_stylesheet().
 *
 * Returns: (transfer full) (element-type GFile): the list of stylesheet files
 *          that were loaded with st_theme_load_stylesheet()
 */
GSList*
st_theme_get_custom_stylesheets (StTheme *theme)
{
  GSList *result = NULL;
  GSList *iter;

  for (iter = theme->custom_stylesheets; iter; iter = iter->next)
    {
      CRStyleSheet *stylesheet = iter->data;
      GFile *file = g_hash_table_lookup (theme->files_by_stylesheet, stylesheet);

      result = g_slist_prepend (result, g_object_ref (file));
    }

  return result;
}

static void
st_theme_constructed (GObject *object)
{
  StTheme *theme = ST_THEME (object);
  CRStyleSheet *application_stylesheet;
  CRStyleSheet *theme_stylesheet;
  CRStyleSheet *default_stylesheet;

  G_OBJECT_CLASS (st_theme_parent_class)->constructed (object);

  application_stylesheet = parse_stylesheet_nofail (theme->application_stylesheet);
  theme_stylesheet = parse_stylesheet_nofail (theme->theme_stylesheet);
  default_stylesheet = parse_stylesheet_nofail (theme->default_stylesheet);

  theme->cascade = cr_cascade_new (application_stylesheet,
                                   theme_stylesheet,
                                   default_stylesheet);

  if (theme->cascade == NULL)
    g_error ("Out of memory when creating cascade object");

  insert_stylesheet (theme, theme->application_stylesheet, application_stylesheet);
  insert_stylesheet (theme, theme->theme_stylesheet, theme_stylesheet);
  insert_stylesheet (theme, theme->default_stylesheet, default_stylesheet);
}

static void
st_theme_finalize (GObject * object)
{
  StTheme *theme = ST_THEME (object);

  g_slist_foreach (theme->custom_stylesheets, (GFunc) cr_stylesheet_unref, NULL);
  g_slist_free (theme->custom_stylesheets);
  theme->custom_stylesheets = NULL;

  g_hash_table_destroy (theme->stylesheets_by_file);
  g_hash_table_destroy (theme->files_by_stylesheet);

  g_clear_object (&theme->application_stylesheet);
  g_clear_object (&theme->theme_stylesheet);
  g_clear_object (&theme->default_stylesheet);

  if (theme->cascade)
    {
      cr_cascade_unref (theme->cascade);
      theme->cascade = NULL;
    }

  G_OBJECT_CLASS (st_theme_parent_class)->finalize (object);
}

static void
st_theme_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  StTheme *theme = ST_THEME (object);

  switch (prop_id)
    {
    case PROP_APPLICATION_STYLESHEET:
      {
        GFile *file = g_value_get_object (value);

        if (!file_equal0 (file, theme->application_stylesheet))
          {
            g_clear_object (&theme->application_stylesheet);
            if (file != NULL)
              theme->application_stylesheet = g_object_ref (file);
          }

        break;
      }
    case PROP_THEME_STYLESHEET:
      {
        GFile *file = g_value_get_object (value);

        if (!file_equal0 (file, theme->theme_stylesheet))
          {
            g_clear_object (&theme->theme_stylesheet);
            if (file != NULL)
              theme->theme_stylesheet = g_object_ref (file);
          }

        break;
      }
    case PROP_DEFAULT_STYLESHEET:
      {
        GFile *file = g_value_get_object (value);

        if (!file_equal0 (file, theme->default_stylesheet))
          {
            g_clear_object (&theme->default_stylesheet);
            if (file != NULL)
              theme->default_stylesheet = g_object_ref (file);
          }

        break;
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
st_theme_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  StTheme *theme = ST_THEME (object);

  switch (prop_id)
    {
    case PROP_APPLICATION_STYLESHEET:
      g_value_set_object (value, theme->application_stylesheet);
      break;
    case PROP_THEME_STYLESHEET:
      g_value_set_object (value, theme->theme_stylesheet);
      break;
    case PROP_DEFAULT_STYLESHEET:
      g_value_set_object (value, theme->default_stylesheet);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/**
 * st_theme_new:
 * @application_stylesheet: The highest priority stylesheet, representing application-specific
 *   styling; this is associated with the CSS "author" stylesheet, may be %NULL
 * @theme_stylesheet: The second priority stylesheet, representing theme-specific styling ;
 *   this is associated with the CSS "user" stylesheet, may be %NULL
 * @default_stylesheet: The lowest priority stylesheet, representing global default styling;
 *   this is associated with the CSS "user agent" stylesheet, may be %NULL
 *
 * Returns: the newly created theme object
 **/
StTheme *
st_theme_new (GFile       *application_stylesheet,
              GFile       *theme_stylesheet,
              GFile       *default_stylesheet)
{
  StTheme *theme = g_object_new (ST_TYPE_THEME,
                                    "application-stylesheet", application_stylesheet,
                                    "theme-stylesheet", theme_stylesheet,
                                    "default-stylesheet", default_stylesheet,
                                    NULL);

  return theme;
}

static gboolean
string_in_list (GString    *stryng,
                GStrv       list)
{
  gchar **it;

  if (list == NULL)
    return FALSE;

  for (it = list; *it != NULL; it++)
    {
      if (!strqcmp (*it, stryng->str, stryng->len))
        return TRUE;
    }

  return FALSE;
}

static gboolean
pseudo_class_add_sel_matches_style (StTheme         *a_this,
                                    CRAdditionalSel *a_add_sel,
                                    StThemeNode     *a_node)
{
  GStrv node_pseudo_classes;

  g_return_val_if_fail (a_this
                        && a_add_sel
                        && a_add_sel->content.pseudo
                        && a_add_sel->content.pseudo->name
                        && a_add_sel->content.pseudo->name->stryng
                        && a_add_sel->content.pseudo->name->stryng->str
                        && a_node, FALSE);

  node_pseudo_classes = st_theme_node_get_pseudo_classes (a_node);

  return string_in_list (a_add_sel->content.pseudo->name->stryng,
                         node_pseudo_classes);
}

/**
 * class_add_sel_matches_style:
 * @a_add_sel: The class additional selector to consider.
 * @a_node: The style node to consider.
 *
 * Returns: %TRUE if the class additional selector matches
 * the style node given in argument, %FALSE otherwise.
 */
static gboolean
class_add_sel_matches_style (CRAdditionalSel *a_add_sel,
                             StThemeNode     *a_node)
{
  GStrv element_classes;

  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == CLASS_ADD_SELECTOR
                        && a_add_sel->content.class_name
                        && a_add_sel->content.class_name->stryng
                        && a_add_sel->content.class_name->stryng->str
                        && a_node, FALSE);

  element_classes = st_theme_node_get_element_classes (a_node);

  return string_in_list (a_add_sel->content.class_name->stryng,
                         element_classes);
}

/*
 *@return TRUE if the additional attribute selector matches
 *the current style node given in argument, FALSE otherwise.
 *@param a_add_sel the additional attribute selector to consider.
 *@param a_node the style node to consider.
 */
static gboolean
id_add_sel_matches_style (CRAdditionalSel *a_add_sel,
                          StThemeNode     *a_node)
{
  gboolean result = FALSE;
  const char *id;

  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == ID_ADD_SELECTOR
                        && a_add_sel->content.id_name
                        && a_add_sel->content.id_name->stryng
                        && a_add_sel->content.id_name->stryng->str
                        && a_node, FALSE);
  g_return_val_if_fail (a_add_sel
                        && a_add_sel->type == ID_ADD_SELECTOR
                        && a_node, FALSE);

  id = st_theme_node_get_element_id (a_node);

  if (id != NULL)
    {
      if (!strqcmp (id, a_add_sel->content.id_name->stryng->str,
                    a_add_sel->content.id_name->stryng->len))
        {
          result = TRUE;
        }
    }

  return result;
}

/**
 *additional_selector_matches_style:
 *Evaluates if a given additional selector matches an style node.
 *@param a_add_sel the additional selector to consider.
 *@param a_node the style node to consider.
 *@return TRUE is a_add_sel matches a_node, FALSE otherwise.
 */
static gboolean
additional_selector_matches_style (StTheme         *a_this,
                                   CRAdditionalSel *a_add_sel,
                                   StThemeNode     *a_node)
{
  CRAdditionalSel *cur_add_sel = NULL;

  g_return_val_if_fail (a_add_sel, FALSE);

  for (cur_add_sel = a_add_sel; cur_add_sel; cur_add_sel = cur_add_sel->next)
    {
      switch (cur_add_sel->type)
        {
        case NO_ADD_SELECTOR:
          return FALSE;
        case CLASS_ADD_SELECTOR:
          if (!class_add_sel_matches_style (cur_add_sel, a_node))
            return FALSE;
          break;
        case ID_ADD_SELECTOR:
          if (!id_add_sel_matches_style (cur_add_sel, a_node))
            return FALSE;
          break;
        case ATTRIBUTE_ADD_SELECTOR:
          g_warning ("Attribute selectors not supported");
          return FALSE;
        case  PSEUDO_CLASS_ADD_SELECTOR:
          if (!pseudo_class_add_sel_matches_style (a_this, cur_add_sel, a_node))
            return FALSE;
          break;
        default:
          g_warning ("Unhandled selector type %d", cur_add_sel->type);
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
element_name_matches_type (const char *element_name,
                           GType       element_type)
{
  if (element_type == G_TYPE_NONE)
    {
      return strcmp (element_name, "stage") == 0;
    }
  else
    {
      GType match_type = g_type_from_name (element_name);
      if (match_type == G_TYPE_INVALID)
        return FALSE;

      return g_type_is_a (element_type, match_type);
    }
}

/*
 *Evaluate a selector (a simple selectors list) and says
 *if it matches the style node given in parameter.
 *The algorithm used here is the following:
 *Walk the combinator separated list of simple selectors backward, starting
 *from the end of the list. For each simple selector, looks if
 *if matches the current style.
 *
 *@param a_this the selection engine.
 *@param a_sel the simple selection list.
 *@param a_node the style node.
 *@param a_result out parameter. Set to true if the
 *selector matches the style node, FALSE otherwise.
 *@param a_recurse if set to TRUE, the function will walk to
 *the next simple selector (after the evaluation of the current one)
 *and recursively evaluate it. Must be usually set to TRUE unless you
 *know what you are doing.
 */
static enum CRStatus
sel_matches_style_real (StTheme     *a_this,
                        CRSimpleSel *a_sel,
                        StThemeNode *a_node,
                        gboolean    *a_result,
                        gboolean     a_eval_sel_list_from_end,
                        gboolean     a_recurse)
{
  CRSimpleSel *cur_sel = NULL;
  StThemeNode *cur_node = NULL;
  GType cur_type;

  *a_result = FALSE;

  if (a_eval_sel_list_from_end)
    {
      /*go and get the last simple selector of the list */
      for (cur_sel = a_sel; cur_sel && cur_sel->next; cur_sel = cur_sel->next)
        ;
    }
  else
    {
      cur_sel = a_sel;
    }

  cur_node = a_node;
  cur_type = st_theme_node_get_element_type (cur_node);

  while (cur_sel)
    {
      if (((cur_sel->type_mask & TYPE_SELECTOR)
           && (cur_sel->name
               && cur_sel->name->stryng
               && cur_sel->name->stryng->str)
           &&
           (element_name_matches_type (cur_sel->name->stryng->str, cur_type)))
          || (cur_sel->type_mask & UNIVERSAL_SELECTOR))
        {
          /*
           *this simple selector
           *matches the current style node
           *Let's see if the preceding
           *simple selectors also match
           *their style node counterpart.
           */
          if (cur_sel->add_sel)
            {
              if (additional_selector_matches_style (a_this, cur_sel->add_sel, cur_node))
                goto walk_a_step_in_expr;
              else
                goto done;
            }
          else
            goto walk_a_step_in_expr;
        }
      if (!(cur_sel->type_mask & TYPE_SELECTOR)
          && !(cur_sel->type_mask & UNIVERSAL_SELECTOR))
        {
          if (!cur_sel->add_sel)
            goto done;
          if (additional_selector_matches_style (a_this, cur_sel->add_sel, cur_node))
            goto walk_a_step_in_expr;
          else
            goto done;
        }
      else
        {
          goto done;
        }

    walk_a_step_in_expr:
      if (a_recurse == FALSE)
        {
          *a_result = TRUE;
          goto done;
        }

      /*
       *here, depending on the combinator of cur_sel
       *choose the axis of the element tree traversal
       *and walk one step in the element tree.
       */
      if (!cur_sel->prev)
        break;

      switch (cur_sel->combinator)
        {
        case NO_COMBINATOR:
          break;

        case COMB_WS:           /*descendant selector */
          {
            StThemeNode *n = NULL;

            /*
             *walk the element tree upward looking for a parent
             *style that matches the preceding selector.
             */
            for (n = st_theme_node_get_parent (a_node); n; n = st_theme_node_get_parent (n))
              {
                enum CRStatus status;
                gboolean matches = FALSE;

                status = sel_matches_style_real (a_this, cur_sel->prev, n, &matches, FALSE, TRUE);

                if (status != CR_OK)
                  goto done;

                if (matches)
                  {
                    cur_node = n;
                    cur_type = st_theme_node_get_element_type (cur_node);
                    break;
                  }
              }

            if (!n)
              {
                /*
                 *didn't find any ancestor that matches
                 *the previous simple selector.
                 */
                goto done;
              }
            /*
             *in this case, the preceding simple sel
             *will have been interpreted twice, which
             *is a cpu and mem waste ... I need to find
             *another way to do this. Anyway, this is
             *my first attempt to write this function and
             *I am a bit clueless.
             */
            break;
          }

        case COMB_PLUS:
          g_warning ("+ combinators are not supported");
          goto done;

        case COMB_GT:
          cur_node = st_theme_node_get_parent (cur_node);
          if (!cur_node)
            goto done;
          cur_type = st_theme_node_get_element_type (cur_node);
          break;

        default:
          goto done;
        }

      cur_sel = cur_sel->prev;
    }

  /*
   *if we reached this point, it means the selector matches
   *the style node.
   */
  *a_result = TRUE;

done:
  return CR_OK;
}

static void
add_matched_properties (StTheme      *a_this,
                        CRStyleSheet *a_nodesheet,
                        StThemeNode  *a_node,
                        GPtrArray    *props)
{
  CRStatement *cur_stmt = NULL;
  CRSelector *sel_list = NULL;
  CRSelector *cur_sel = NULL;
  gboolean matches = FALSE;
  enum CRStatus status = CR_OK;

  /*
   *walk through the list of statements and,
   *get the selectors list inside the statements that
   *contain some, and try to match our style node in these
   *selectors lists.
   */
  for (cur_stmt = a_nodesheet->statements; cur_stmt; cur_stmt = cur_stmt->next)
    {
      /*
       *initialize the selector list in which we will
       *really perform the search.
       */
      sel_list = NULL;

      /*
       *get the the damn selector list in
       *which we have to look
       */
      switch (cur_stmt->type)
        {
        case RULESET_STMT:
          if (cur_stmt->kind.ruleset && cur_stmt->kind.ruleset->sel_list)
            {
              sel_list = cur_stmt->kind.ruleset->sel_list;
            }
          break;

        case AT_IMPORT_RULE_STMT:
          {
            CRAtImportRule *import_rule = cur_stmt->kind.import_rule;

            if (import_rule->sheet == NULL)
              {
                CRStyleSheet *sheet = NULL;
                GFile *file = NULL;

                if (import_rule->url->stryng && import_rule->url->stryng->str)
                  {
                    file = _st_theme_resolve_url (a_this,
                                                  a_nodesheet,
                                                  import_rule->url->stryng->str);
                    sheet = resolve_stylesheet (a_this, file, NULL);
                  }

                if (sheet)
                  {
                    import_rule->sheet = sheet;
                  }
                else
                  {
                    /* Set a marker to avoid repeatedly trying to parse a non-existent or
                     * broken stylesheet
                     */
                    import_rule->sheet = (CRStyleSheet *) - 1;
                  }

                if (file)
                  g_object_unref (file);
              }

            if (import_rule->sheet != (CRStyleSheet *) - 1)
              {
                add_matched_properties (a_this, import_rule->sheet,
                                        a_node, props);
              }
          }
          break;
        case AT_MEDIA_RULE_STMT:
        case AT_RULE_STMT:
        case AT_PAGE_RULE_STMT:
        case AT_CHARSET_RULE_STMT:
        case AT_FONT_FACE_RULE_STMT:
        default:
          break;
        }

      if (!sel_list)
        continue;

      /*
       *now, we have a comma separated selector list to look in.
       *let's walk it and try to match the style node
       *on each item of the list.
       */
      for (cur_sel = sel_list; cur_sel; cur_sel = cur_sel->next)
        {
          if (!cur_sel->simple_sel)
            continue;

          status = sel_matches_style_real (a_this, cur_sel->simple_sel, a_node, &matches, TRUE, TRUE);

          if (status == CR_OK && matches)
            {
              CRDeclaration *cur_decl = NULL;

              /* In order to sort the matching properties, we need to compute the
               * specificity of the selector that actually matched this
               * element. In a non-thread-safe fashion, we store it in the
               * ruleset. (Fixing this would mean cut-and-pasting
               * cr_simple_sel_compute_specificity(), and have no need for
               * thread-safety anyways.)
               *
               * Once we've sorted the properties, the specificity no longer
               * matters and it can be safely overridden.
               */
              cr_simple_sel_compute_specificity (cur_sel->simple_sel);

              cur_stmt->specificity = cur_sel->simple_sel->specificity;

              for (cur_decl = cur_stmt->kind.ruleset->decl_list; cur_decl; cur_decl = cur_decl->next)
                g_ptr_array_add (props, cur_decl);
            }
        }
    }
}

#define ORIGIN_OFFSET_IMPORTANT (NB_ORIGINS)
#define ORIGIN_OFFSET_EXTENSION (NB_ORIGINS * 2)

static inline int
get_origin (const CRDeclaration * decl)
{
  enum CRStyleOrigin origin = decl->parent_statement->parent_sheet->origin;
  gboolean is_extension_sheet = GPOINTER_TO_UINT (decl->parent_statement->parent_sheet->app_data);

  if (decl->important)
    origin += ORIGIN_OFFSET_IMPORTANT;

  if (is_extension_sheet)
    origin += ORIGIN_OFFSET_EXTENSION;

  return origin;
}

/* Order of comparison is so that higher priority statements compare after
 * lower priority statements */
static int
compare_declarations (gconstpointer a,
                      gconstpointer b)
{
  /* g_ptr_array_sort() is broooken */
  CRDeclaration *decl_a = *(CRDeclaration **) a;
  CRDeclaration *decl_b = *(CRDeclaration **) b;

  int origin_a = get_origin (decl_a);
  int origin_b = get_origin (decl_b);

  if (origin_a != origin_b)
    return origin_a - origin_b;

  if (decl_a->parent_statement->specificity != decl_b->parent_statement->specificity)
    return decl_a->parent_statement->specificity - decl_b->parent_statement->specificity;

  return 0;
}

GPtrArray *
_st_theme_get_matched_properties (StTheme        *theme,
                                  StThemeNode    *node)
{
  enum CRStyleOrigin origin = 0;
  CRStyleSheet *sheet = NULL;
  GPtrArray *props = g_ptr_array_new ();
  GSList *iter;

  g_return_val_if_fail (ST_IS_THEME (theme), NULL);
  g_return_val_if_fail (ST_IS_THEME_NODE (node), NULL);

  for (origin = ORIGIN_UA; origin < NB_ORIGINS; origin++)
    {
      sheet = cr_cascade_get_sheet (theme->cascade, origin);
      if (!sheet)
        continue;

      add_matched_properties (theme, sheet, node, props);
    }

  for (iter = theme->custom_stylesheets; iter; iter = iter->next)
    add_matched_properties (theme, iter->data, node, props);

  /* We count on a stable sort here so that later declarations come
   * after earlier declarations */
  g_ptr_array_sort (props, compare_declarations);

  return props;
}

/* Resolve an url from an url() reference in a stylesheet into a GFile,
 * if possible. The resolution here is distinctly lame and
 * will fail on many examples.
 */
GFile *
_st_theme_resolve_url (StTheme      *theme,
                       CRStyleSheet *base_stylesheet,
                       const char   *url)
{
  char *scheme;
  GFile *resource;

  if ((scheme = g_uri_parse_scheme (url)))
    {
      g_free (scheme);
      resource = g_file_new_for_uri (url);
    }
  else if (base_stylesheet != NULL)
    {
      GFile *base_file = NULL, *parent;

      base_file = g_hash_table_lookup (theme->files_by_stylesheet, base_stylesheet);

      /* This is an internal function, if we get here with
         a bad @base_stylesheet we have a problem. */
      g_assert (base_file);

      parent = g_file_get_parent (base_file);
      resource = g_file_resolve_relative_path (parent, url);

      g_object_unref (parent);
    }
  else
    {
      resource = g_file_new_for_path (url);
    }

  return resource;
}

/**
 * st_theme_get_application_stylesheet:
 *
 * Returns: (transfer none): the stylesheet
 */
GFile *
st_theme_get_application_stylesheet (StTheme *theme)
{
  g_return_val_if_fail (ST_IS_THEME (theme), NULL);

  return theme->application_stylesheet;
}

/**
 * st_theme_get_theme_stylesheet:
 *
 * Returns: (transfer none): the stylesheet
 */
GFile *
st_theme_get_theme_stylesheet (StTheme *theme)
{
  g_return_val_if_fail (ST_IS_THEME (theme), NULL);

  return theme->theme_stylesheet;
}

/**
 * st_theme_get_default_stylesheet:
 *
 * Returns: (transfer none): the stylesheet
 */
GFile *
st_theme_get_default_stylesheet (StTheme *theme)
{
  g_return_val_if_fail (ST_IS_THEME (theme), NULL);

  return theme->default_stylesheet;
}
