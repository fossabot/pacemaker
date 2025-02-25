/*
 * Copyright 2004-2019 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>
#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/xml.h>

#include <glib.h>

#include <crm/pengine/rules.h>
#include <crm/pengine/rules_internal.h>
#include <crm/pengine/internal.h>

#include <sys/types.h>
#include <regex.h>
#include <ctype.h>

CRM_TRACE_INIT_DATA(pe_rules);

gboolean
test_ruleset(xmlNode * ruleset, GHashTable * node_hash, crm_time_t * now)
{
    gboolean ruleset_default = TRUE;
    xmlNode *rule = NULL;

    for (rule = __xml_first_child_element(ruleset); rule != NULL;
         rule = __xml_next_element(rule)) {

        if (crm_str_eq((const char *)rule->name, XML_TAG_RULE, TRUE)) {
            ruleset_default = FALSE;
            if (test_rule(rule, node_hash, RSC_ROLE_UNKNOWN, now)) {
                return TRUE;
            }
        }
    }

    return ruleset_default;
}

gboolean
test_rule(xmlNode * rule, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now)
{
    return pe_test_rule_full(rule, node_hash, role, now, NULL);
}

gboolean
pe_test_rule_re(xmlNode * rule, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now, pe_re_match_data_t * re_match_data)
{
    pe_match_data_t match_data = {
                                    .re = re_match_data,
                                    .params = NULL,
                                    .meta = NULL,
                                 };
    return pe_test_rule_full(rule, node_hash, role, now, &match_data);
}

gboolean
pe_test_rule_full(xmlNode * rule, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now, pe_match_data_t * match_data)
{
    xmlNode *expr = NULL;
    gboolean test = TRUE;
    gboolean empty = TRUE;
    gboolean passed = TRUE;
    gboolean do_and = TRUE;
    const char *value = NULL;

    rule = expand_idref(rule, NULL);
    value = crm_element_value(rule, XML_RULE_ATTR_BOOLEAN_OP);
    if (safe_str_eq(value, "or")) {
        do_and = FALSE;
        passed = FALSE;
    }

    crm_trace("Testing rule %s", ID(rule));
    for (expr = __xml_first_child_element(rule); expr != NULL;
         expr = __xml_next_element(expr)) {

        test = pe_test_expression_full(expr, node_hash, role, now, match_data);
        empty = FALSE;

        if (test && do_and == FALSE) {
            crm_trace("Expression %s/%s passed", ID(rule), ID(expr));
            return TRUE;

        } else if (test == FALSE && do_and) {
            crm_trace("Expression %s/%s failed", ID(rule), ID(expr));
            return FALSE;
        }
    }

    if (empty) {
        crm_err("Invalid Rule %s: rules must contain at least one expression", ID(rule));
    }

    crm_trace("Rule %s %s", ID(rule), passed ? "passed" : "failed");
    return passed;
}

gboolean
test_expression(xmlNode * expr, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now)
{
    return pe_test_expression_full(expr, node_hash, role, now, NULL);
}

gboolean
pe_test_expression_re(xmlNode * expr, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now, pe_re_match_data_t * re_match_data)
{
    pe_match_data_t match_data = {
                                    .re = re_match_data,
                                    .params = NULL,
                                    .meta = NULL,
                                 };
    return pe_test_expression_full(expr, node_hash, role, now, &match_data);
}

gboolean
pe_test_expression_full(xmlNode * expr, GHashTable * node_hash, enum rsc_role_e role, crm_time_t * now, pe_match_data_t * match_data)
{
    gboolean accept = FALSE;
    const char *uname = NULL;

    switch (find_expression_type(expr)) {
        case nested_rule:
            accept = pe_test_rule_full(expr, node_hash, role, now, match_data);
            break;
        case attr_expr:
        case loc_expr:
            /* these expressions can never succeed if there is
             * no node to compare with
             */
            if (node_hash != NULL) {
                accept = pe_test_attr_expression_full(expr, node_hash, now, match_data);
            }
            break;

        case time_expr:
            accept = pe_test_date_expression(expr, now);
            break;

        case role_expr:
            accept = pe_test_role_expression(expr, role, now);
            break;

#ifdef ENABLE_VERSIONED_ATTRS
        case version_expr:
            if (node_hash && g_hash_table_lookup_extended(node_hash,
                                                          CRM_ATTR_RA_VERSION,
                                                          NULL, NULL)) {
                accept = pe_test_attr_expression(expr, node_hash, now);
            } else {
                // we are going to test it when we have ra-version
                accept = TRUE;
            }
            break;
#endif

        default:
            CRM_CHECK(FALSE /* bad type */ , return FALSE);
            accept = FALSE;
    }
    if (node_hash) {
        uname = g_hash_table_lookup(node_hash, CRM_ATTR_UNAME);
    }

    crm_trace("Expression %s %s on %s",
              ID(expr), accept ? "passed" : "failed", uname ? uname : "all nodes");
    return accept;
}

enum expression_type
find_expression_type(xmlNode * expr)
{
    const char *tag = NULL;
    const char *attr = NULL;

    attr = crm_element_value(expr, XML_EXPR_ATTR_ATTRIBUTE);
    tag = crm_element_name(expr);

    if (safe_str_eq(tag, "date_expression")) {
        return time_expr;

    } else if (safe_str_eq(tag, XML_TAG_RULE)) {
        return nested_rule;

    } else if (safe_str_neq(tag, "expression")) {
        return not_expr;

    } else if (safe_str_eq(attr, CRM_ATTR_UNAME)
               || safe_str_eq(attr, CRM_ATTR_KIND)
               || safe_str_eq(attr, CRM_ATTR_ID)) {
        return loc_expr;

    } else if (safe_str_eq(attr, CRM_ATTR_ROLE)) {
        return role_expr;

#ifdef ENABLE_VERSIONED_ATTRS
    } else if (safe_str_eq(attr, CRM_ATTR_RA_VERSION)) {
        return version_expr;
#endif
    }

    return attr_expr;
}

gboolean
pe_test_role_expression(xmlNode * expr, enum rsc_role_e role, crm_time_t * now)
{
    gboolean accept = FALSE;
    const char *op = NULL;
    const char *value = NULL;

    if (role == RSC_ROLE_UNKNOWN) {
        return accept;
    }

    value = crm_element_value(expr, XML_EXPR_ATTR_VALUE);
    op = crm_element_value(expr, XML_EXPR_ATTR_OPERATION);

    if (safe_str_eq(op, "defined")) {
        if (role > RSC_ROLE_STARTED) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "not_defined")) {
        if (role < RSC_ROLE_SLAVE && role > RSC_ROLE_UNKNOWN) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "eq")) {
        if (text2role(value) == role) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "ne")) {
        // Test "ne" only with promotable clone roles
        if (role < RSC_ROLE_SLAVE && role > RSC_ROLE_UNKNOWN) {
            accept = FALSE;

        } else if (text2role(value) != role) {
            accept = TRUE;
        }
    }
    return accept;
}

gboolean
pe_test_attr_expression(xmlNode * expr, GHashTable * hash, crm_time_t * now)
{
    return pe_test_attr_expression_full(expr, hash, now, NULL);
}

gboolean
pe_test_attr_expression_full(xmlNode * expr, GHashTable * hash, crm_time_t * now, pe_match_data_t * match_data)
{
    gboolean accept = FALSE;
    gboolean attr_allocated = FALSE;
    int cmp = 0;
    const char *h_val = NULL;
    GHashTable *table = NULL;

    const char *op = NULL;
    const char *type = NULL;
    const char *attr = NULL;
    const char *value = NULL;
    const char *value_source = NULL;

    attr = crm_element_value(expr, XML_EXPR_ATTR_ATTRIBUTE);
    op = crm_element_value(expr, XML_EXPR_ATTR_OPERATION);
    value = crm_element_value(expr, XML_EXPR_ATTR_VALUE);
    type = crm_element_value(expr, XML_EXPR_ATTR_TYPE);
    value_source = crm_element_value(expr, XML_EXPR_ATTR_VALUE_SOURCE);

    if (attr == NULL || op == NULL) {
        pe_err("Invalid attribute or operation in expression"
               " (\'%s\' \'%s\' \'%s\')", crm_str(attr), crm_str(op), crm_str(value));
        return FALSE;
    }

    if (match_data) {
        if (match_data->re) {
            char *resolved_attr = pe_expand_re_matches(attr, match_data->re);

            if (resolved_attr) {
                attr = (const char *) resolved_attr;
                attr_allocated = TRUE;
            }
        }

        if (safe_str_eq(value_source, "param")) {
            table = match_data->params;
        } else if (safe_str_eq(value_source, "meta")) {
            table = match_data->meta;
        }
    }

    if (table) {
        const char *param_name = value;
        const char *param_value = NULL;

        if (param_name && param_name[0]) {
            if ((param_value = (const char *)g_hash_table_lookup(table, param_name))) {
                value = param_value;
            }
        }
    }

    if (hash != NULL) {
        h_val = (const char *)g_hash_table_lookup(hash, attr);
    }

    if (attr_allocated) {
        free((char *)attr);
        attr = NULL;
    }

    if (value != NULL && h_val != NULL) {
        if (type == NULL) {
            if (safe_str_eq(op, "lt")
                || safe_str_eq(op, "lte")
                || safe_str_eq(op, "gt")
                || safe_str_eq(op, "gte")) {
                type = "number";

            } else {
                type = "string";
            }
            crm_trace("Defaulting to %s based comparison for '%s' op", type, op);
        }

        if (safe_str_eq(type, "string")) {
            cmp = strcasecmp(h_val, value);

        } else if (safe_str_eq(type, "number")) {
            int h_val_f = crm_parse_int(h_val, NULL);
            int value_f = crm_parse_int(value, NULL);

            if (h_val_f < value_f) {
                cmp = -1;
            } else if (h_val_f > value_f) {
                cmp = 1;
            } else {
                cmp = 0;
            }

        } else if (safe_str_eq(type, "version")) {
            cmp = compare_version(h_val, value);

        }

    } else if (value == NULL && h_val == NULL) {
        cmp = 0;
    } else if (value == NULL) {
        cmp = 1;
    } else {
        cmp = -1;
    }

    if (safe_str_eq(op, "defined")) {
        if (h_val != NULL) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "not_defined")) {
        if (h_val == NULL) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "eq")) {
        if ((h_val == value) || cmp == 0) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "ne")) {
        if ((h_val == NULL && value != NULL)
            || (h_val != NULL && value == NULL)
            || cmp != 0) {
            accept = TRUE;
        }

    } else if (value == NULL || h_val == NULL) {
        // The comparison is meaningless from this point on
        accept = FALSE;

    } else if (safe_str_eq(op, "lt")) {
        if (cmp < 0) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "lte")) {
        if (cmp <= 0) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "gt")) {
        if (cmp > 0) {
            accept = TRUE;
        }

    } else if (safe_str_eq(op, "gte")) {
        if (cmp >= 0) {
            accept = TRUE;
        }
    }

    return accept;
}

/* As per the nethack rules:
 *
 * moon period = 29.53058 days ~= 30, year = 365.2422 days
 * days moon phase advances on first day of year compared to preceding year
 *      = 365.2422 - 12*29.53058 ~= 11
 * years in Metonic cycle (time until same phases fall on the same days of
 *      the month) = 18.6 ~= 19
 * moon phase on first day of year (epact) ~= (11*(year%19) + 29) % 30
 *      (29 as initial condition)
 * current phase in days = first day phase + days elapsed in year
 * 6 moons ~= 177 days
 * 177 ~= 8 reported phases * 22
 * + 11/22 for rounding
 *
 * 0-7, with 0: new, 4: full
 */

static int
phase_of_the_moon(crm_time_t * now)
{
    uint32_t epact, diy, goldn;
    uint32_t y;

    crm_time_get_ordinal(now, &y, &diy);

    goldn = (y % 19) + 1;
    epact = (11 * goldn + 18) % 30;
    if ((epact == 25 && goldn > 11) || epact == 24)
        epact++;

    return ((((((diy + epact) * 6) + 11) % 177) / 22) & 7);
}

static gboolean
decodeNVpair(const char *srcstring, char separator, char **name, char **value)
{
    const char *seploc = NULL;

    CRM_ASSERT(name != NULL && value != NULL);
    *name = NULL;
    *value = NULL;

    crm_trace("Attempting to decode: [%s]", srcstring);
    if (srcstring != NULL) {
        seploc = strchr(srcstring, separator);
        if (seploc) {
            *name = strndup(srcstring, seploc - srcstring);
            if (*(seploc + 1)) {
                *value = strdup(seploc + 1);
            }
            return TRUE;
        }
    }
    return FALSE;
}

#define cron_check(xml_field, time_field)				\
    value = crm_element_value(cron_spec, xml_field);			\
    if(value != NULL) {							\
	gboolean pass = TRUE;						\
	decodeNVpair(value, '-', &value_low, &value_high);		\
	if(value_low == NULL) {						\
	    value_low = strdup(value);				\
	}								\
	value_low_i = crm_parse_int(value_low, "0");			\
	value_high_i = crm_parse_int(value_high, "-1");			\
	if(value_high_i < 0) {						\
	    if(value_low_i != time_field) {				\
		pass = FALSE;						\
	    }								\
	} else if(value_low_i > time_field) {				\
	    pass = FALSE;						\
	} else if(value_high_i < time_field) {				\
	    pass = FALSE;						\
	}								\
	free(value_low);						\
	free(value_high);						\
	if(pass == FALSE) {						\
	    crm_debug("Condition '%s' in %s: failed", value, xml_field); \
	    return pass;						\
	}								\
	crm_debug("Condition '%s' in %s: passed", value, xml_field);	\
    }

gboolean
pe_cron_range_satisfied(crm_time_t * now, xmlNode * cron_spec)
{
    const char *value = NULL;
    char *value_low = NULL;
    char *value_high = NULL;

    int value_low_i = 0;
    int value_high_i = 0;

    uint32_t h, m, s, y, d, w;

    CRM_CHECK(now != NULL, return FALSE);

    crm_time_get_timeofday(now, &h, &m, &s);

    cron_check("seconds", s);
    cron_check("minutes", m);
    cron_check("hours", h);

    crm_time_get_gregorian(now, &y, &m, &d);

    cron_check("monthdays", d);
    cron_check("months", m);
    cron_check("years", y);

    crm_time_get_ordinal(now, &y, &d);

    cron_check("yeardays", d);

    crm_time_get_isoweek(now, &y, &w, &d);

    cron_check("weekyears", y);
    cron_check("weeks", w);
    cron_check("weekdays", d);

    cron_check("moon", phase_of_the_moon(now));

    return TRUE;
}

#define update_field(xml_field, time_fn)			\
    value = crm_element_value(duration_spec, xml_field);	\
    if(value != NULL) {						\
	int value_i = crm_parse_int(value, "0");		\
	time_fn(end, value_i);					\
    }

crm_time_t *
pe_parse_xml_duration(crm_time_t * start, xmlNode * duration_spec)
{
    crm_time_t *end = NULL;
    const char *value = NULL;

    end = crm_time_new(NULL);
    crm_time_set(end, start);

    update_field("years", crm_time_add_years);
    update_field("months", crm_time_add_months);
    update_field("weeks", crm_time_add_weeks);
    update_field("days", crm_time_add_days);
    update_field("hours", crm_time_add_hours);
    update_field("minutes", crm_time_add_minutes);
    update_field("seconds", crm_time_add_seconds);

    return end;
}

gboolean
pe_test_date_expression(xmlNode * time_expr, crm_time_t * now)
{
    pe_eval_date_result_t result = pe_eval_date_expression(time_expr, now);
    const char *op = crm_element_value(time_expr, "operation");

    if (result == pe_date_within_range) {
        return TRUE;

    } else if ((safe_str_eq(op, "date_spec") || safe_str_eq(op, "in_range") || op == NULL) &&
               result == pe_date_op_satisfied) {
        return TRUE;

    } else if ((safe_str_eq(op, "eq") || safe_str_eq(op, "neq")) &&
               result == pe_date_op_satisfied) {
        return TRUE;

    } else {
        return FALSE;
    }
}

pe_eval_date_result_t
pe_eval_date_expression(xmlNode * time_expr, crm_time_t * now)
{
    crm_time_t *start = NULL;
    crm_time_t *end = NULL;
    const char *value = NULL;
    const char *op = crm_element_value(time_expr, "operation");

    xmlNode *duration_spec = NULL;
    xmlNode *date_spec = NULL;

    pe_eval_date_result_t rc = pe_date_result_undetermined;

    crm_trace("Testing expression: %s", ID(time_expr));

    duration_spec = first_named_child(time_expr, "duration");
    date_spec = first_named_child(time_expr, "date_spec");

    value = crm_element_value(time_expr, "start");
    if (value != NULL) {
        start = crm_time_new(value);
    }
    value = crm_element_value(time_expr, "end");
    if (value != NULL) {
        end = crm_time_new(value);
    }

    if (start != NULL && end == NULL && duration_spec != NULL) {
        end = pe_parse_xml_duration(start, duration_spec);
    }
    if (op == NULL) {
        op = "in_range";
    }

    if (safe_str_eq(op, "date_spec") || safe_str_eq(op, "in_range")) {
        if (start != NULL && crm_time_compare(start, now) > 0) {
            rc = pe_date_before_range;
        } else if (end != NULL && crm_time_compare(end, now) < 0) {
            rc = pe_date_after_range;
        } else if (safe_str_eq(op, "in_range")) {
            rc = pe_date_within_range;
        } else {
            rc = pe_cron_range_satisfied(now, date_spec) ? pe_date_op_satisfied
                                                         : pe_date_op_unsatisfied;
        }

    } else if (safe_str_eq(op, "gt")) {
        rc = crm_time_compare(start, now) < 0 ? pe_date_within_range : pe_date_before_range;

    } else if (safe_str_eq(op, "lt")) {
        rc = crm_time_compare(end, now) > 0 ? pe_date_within_range : pe_date_after_range;

    } else if (safe_str_eq(op, "eq")) {
        rc = crm_time_compare(start, now) == 0 ? pe_date_op_satisfied
                                               : pe_date_op_unsatisfied;

    } else if (safe_str_eq(op, "neq")) {
        rc = crm_time_compare(start, now) != 0 ? pe_date_op_satisfied
                                               : pe_date_op_unsatisfied;
    }

    crm_time_free(start);
    crm_time_free(end);
    return rc;
}

typedef struct sorted_set_s {
    int score;
    const char *name;
    const char *special_name;
    xmlNode *attr_set;
} sorted_set_t;

static gint
sort_pairs(gconstpointer a, gconstpointer b)
{
    const sorted_set_t *pair_a = a;
    const sorted_set_t *pair_b = b;

    if (a == NULL && b == NULL) {
        return 0;
    } else if (a == NULL) {
        return 1;
    } else if (b == NULL) {
        return -1;
    }

    if (safe_str_eq(pair_a->name, pair_a->special_name)) {
        return -1;

    } else if (safe_str_eq(pair_b->name, pair_a->special_name)) {
        return 1;
    }

    if (pair_a->score < pair_b->score) {
        return 1;
    } else if (pair_a->score > pair_b->score) {
        return -1;
    }
    return 0;
}

static void
populate_hash(xmlNode * nvpair_list, GHashTable * hash, gboolean overwrite, xmlNode * top)
{
    const char *name = NULL;
    const char *value = NULL;
    const char *old_value = NULL;
    xmlNode *list = nvpair_list;
    xmlNode *an_attr = NULL;

    name = crm_element_name(list->children);
    if (safe_str_eq(XML_TAG_ATTRS, name)) {
        list = list->children;
    }

    for (an_attr = __xml_first_child_element(list); an_attr != NULL;
         an_attr = __xml_next_element(an_attr)) {

        if (crm_str_eq((const char *)an_attr->name, XML_CIB_TAG_NVPAIR, TRUE)) {
            xmlNode *ref_nvpair = expand_idref(an_attr, top);

            name = crm_element_value(an_attr, XML_NVPAIR_ATTR_NAME);
            if (name == NULL) {
                name = crm_element_value(ref_nvpair, XML_NVPAIR_ATTR_NAME);
            }

            crm_trace("Setting attribute: %s", name);
            value = crm_element_value(an_attr, XML_NVPAIR_ATTR_VALUE);
            if (value == NULL) {
                value = crm_element_value(ref_nvpair, XML_NVPAIR_ATTR_VALUE);
            }

            if (name == NULL || value == NULL) {
                continue;

            }

            old_value = g_hash_table_lookup(hash, name);

            if (safe_str_eq(value, "#default")) {
                if (old_value) {
                    crm_trace("Removing value for %s (%s)", name, value);
                    g_hash_table_remove(hash, name);
                }
                continue;

            } else if (old_value == NULL) {
                g_hash_table_insert(hash, strdup(name), strdup(value));

            } else if (overwrite) {
                crm_debug("Overwriting value of %s: %s -> %s", name, old_value, value);
                g_hash_table_replace(hash, strdup(name), strdup(value));
            }
        }
    }
}

#ifdef ENABLE_VERSIONED_ATTRS
static xmlNode*
get_versioned_rule(xmlNode * attr_set)
{
    xmlNode * rule = NULL;
    xmlNode * expr = NULL;

    for (rule = __xml_first_child_element(attr_set); rule != NULL;
         rule = __xml_next_element(rule)) {

        if (crm_str_eq((const char *)rule->name, XML_TAG_RULE, TRUE)) {
            for (expr = __xml_first_child_element(rule); expr != NULL;
                 expr = __xml_next_element(expr)) {

                if (find_expression_type(expr) == version_expr) {
                    return rule;
                }
            }
        }
    }

    return NULL;
}

static void
add_versioned_attributes(xmlNode * attr_set, xmlNode * versioned_attrs)
{
    xmlNode *attr_set_copy = NULL;
    xmlNode *rule = NULL;
    xmlNode *expr = NULL;

    if (!attr_set || !versioned_attrs) {
        return;
    }

    attr_set_copy = copy_xml(attr_set);

    rule = get_versioned_rule(attr_set_copy);
    if (!rule) {
        free_xml(attr_set_copy);
        return;
    }

    expr = __xml_first_child_element(rule);
    while (expr != NULL) {
        if (find_expression_type(expr) != version_expr) {
            xmlNode *node = expr;

            expr = __xml_next_element(expr);
            free_xml(node);
        } else {
            expr = __xml_next_element(expr);
        }
    }

    add_node_nocopy(versioned_attrs, NULL, attr_set_copy);
}
#endif

typedef struct unpack_data_s {
    gboolean overwrite;
    GHashTable *node_hash;
    void *hash;
    crm_time_t *now;
    xmlNode *top;
} unpack_data_t;

static void
unpack_attr_set(gpointer data, gpointer user_data)
{
    sorted_set_t *pair = data;
    unpack_data_t *unpack_data = user_data;

    if (test_ruleset(pair->attr_set, unpack_data->node_hash, unpack_data->now) == FALSE) {
        return;
    }

#ifdef ENABLE_VERSIONED_ATTRS
    if (get_versioned_rule(pair->attr_set) && !(unpack_data->node_hash &&
        g_hash_table_lookup_extended(unpack_data->node_hash,
                                     CRM_ATTR_RA_VERSION, NULL, NULL))) {
        // we haven't actually tested versioned expressions yet
        return;
    }
#endif

    crm_trace("Adding attributes from %s", pair->name);
    populate_hash(pair->attr_set, unpack_data->hash, unpack_data->overwrite, unpack_data->top);
}

#ifdef ENABLE_VERSIONED_ATTRS
static void
unpack_versioned_attr_set(gpointer data, gpointer user_data)
{
    sorted_set_t *pair = data;
    unpack_data_t *unpack_data = user_data;

    if (test_ruleset(pair->attr_set, unpack_data->node_hash, unpack_data->now) == FALSE) {
        return;
    }

    add_versioned_attributes(pair->attr_set, unpack_data->hash);
}
#endif

static GListPtr
make_pairs_and_populate_data(xmlNode * top, xmlNode * xml_obj, const char *set_name,
                             GHashTable * node_hash, void * hash, const char *always_first,
                             gboolean overwrite, crm_time_t * now, unpack_data_t * data)
{
    GListPtr unsorted = NULL;
    const char *score = NULL;
    sorted_set_t *pair = NULL;
    xmlNode *attr_set = NULL;

    if (xml_obj == NULL) {
        crm_trace("No instance attributes");
        return NULL;
    }

    crm_trace("Checking for attributes");
    for (attr_set = __xml_first_child_element(xml_obj); attr_set != NULL;
         attr_set = __xml_next_element(attr_set)) {

        /* Uncertain if set_name == NULL check is strictly necessary here */
        if (set_name == NULL || crm_str_eq((const char *)attr_set->name, set_name, TRUE)) {
            pair = NULL;
            attr_set = expand_idref(attr_set, top);
            if (attr_set == NULL) {
                continue;
            }

            pair = calloc(1, sizeof(sorted_set_t));
            pair->name = ID(attr_set);
            pair->special_name = always_first;
            pair->attr_set = attr_set;

            score = crm_element_value(attr_set, XML_RULE_ATTR_SCORE);
            pair->score = char2score(score);

            unsorted = g_list_prepend(unsorted, pair);
        }
    }

    if (pair != NULL) {
        data->hash = hash;
        data->node_hash = node_hash;
        data->now = now;
        data->overwrite = overwrite;
        data->top = top;
    }

    if (unsorted) {
        return g_list_sort(unsorted, sort_pairs);
    }

    return NULL;
}

void
unpack_instance_attributes(xmlNode * top, xmlNode * xml_obj, const char *set_name,
                           GHashTable * node_hash, GHashTable * hash, const char *always_first,
                           gboolean overwrite, crm_time_t * now)
{
    unpack_data_t data;
    GListPtr pairs = make_pairs_and_populate_data(top, xml_obj, set_name, node_hash, hash,
                                                  always_first, overwrite, now, &data);

    if (pairs) {
        g_list_foreach(pairs, unpack_attr_set, &data);
        g_list_free_full(pairs, free);
    }
}

#ifdef ENABLE_VERSIONED_ATTRS
void
pe_unpack_versioned_attributes(xmlNode * top, xmlNode * xml_obj, const char *set_name,
                               GHashTable * node_hash, xmlNode * hash, crm_time_t * now)
{
    unpack_data_t data;
    GListPtr pairs = make_pairs_and_populate_data(top, xml_obj, set_name, node_hash, hash,
                                                  NULL, FALSE, now, &data);

    if (pairs) {
        g_list_foreach(pairs, unpack_versioned_attr_set, &data);
        g_list_free_full(pairs, free);
    }
}
#endif

char *
pe_expand_re_matches(const char *string, pe_re_match_data_t *match_data)
{
    size_t len = 0;
    int i;
    const char *p, *last_match_index;
    char *p_dst, *result = NULL;

    if (!string || string[0] == '\0' || !match_data) {
        return NULL;
    }

    p = last_match_index = string;

    while (*p) {
        if (*p == '%' && *(p + 1) && isdigit(*(p + 1))) {
            i = *(p + 1) - '0';
            if (match_data->nregs >= i && match_data->pmatch[i].rm_so != -1 &&
                match_data->pmatch[i].rm_eo > match_data->pmatch[i].rm_so) {
                len += p - last_match_index + (match_data->pmatch[i].rm_eo - match_data->pmatch[i].rm_so);
                last_match_index = p + 2;
            }
            p++;
        }
        p++;
    }
    len += p - last_match_index + 1;

    /* FIXME: Excessive? */
    if (len - 1 <= 0) {
        return NULL;
    }

    p_dst = result = calloc(1, len);
    p = string;

    while (*p) {
        if (*p == '%' && *(p + 1) && isdigit(*(p + 1))) {
            i = *(p + 1) - '0';
            if (match_data->nregs >= i && match_data->pmatch[i].rm_so != -1 &&
                match_data->pmatch[i].rm_eo > match_data->pmatch[i].rm_so) {
                /* rm_eo can be equal to rm_so, but then there is nothing to do */
                int match_len = match_data->pmatch[i].rm_eo - match_data->pmatch[i].rm_so;
                memcpy(p_dst, match_data->string + match_data->pmatch[i].rm_so, match_len);
                p_dst += match_len;
            }
            p++;
        } else {
            *(p_dst) = *(p);
            p_dst++;
        }
        p++;
    }

    return result;
}

#ifdef ENABLE_VERSIONED_ATTRS
GHashTable*
pe_unpack_versioned_parameters(xmlNode *versioned_params, const char *ra_version)
{
    GHashTable *hash = crm_str_table_new();

    if (versioned_params && ra_version) {
        GHashTable *node_hash = crm_str_table_new();
        xmlNode *attr_set = __xml_first_child_element(versioned_params);

        if (attr_set) {
            g_hash_table_insert(node_hash, strdup(CRM_ATTR_RA_VERSION),
                                strdup(ra_version));
            unpack_instance_attributes(NULL, versioned_params, crm_element_name(attr_set),
                                       node_hash, hash, NULL, FALSE, NULL);
        }

        g_hash_table_destroy(node_hash);
    }

    return hash;
}
#endif
