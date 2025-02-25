/*
 * Copyright 2004-2019 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <crm/pengine/rules.h>
#include <crm/pengine/internal.h>
#include <crm/msg_xml.h>

#include <unpack.h>

void populate_hash(xmlNode * nvpair_list, GHashTable * hash, const char **attrs, int attrs_length);

resource_object_functions_t resource_class_functions[] = {
    {
     native_unpack,
     native_find_rsc,
     native_parameter,
     native_print,
     native_active,
     native_resource_state,
     native_location,
     native_free
    },
    {
     group_unpack,
     native_find_rsc,
     native_parameter,
     group_print,
     group_active,
     group_resource_state,
     native_location,
     group_free
    },
    {
     clone_unpack,
     native_find_rsc,
     native_parameter,
     clone_print,
     clone_active,
     clone_resource_state,
     native_location,
     clone_free
    },
    {
     pe__unpack_bundle,
     native_find_rsc,
     native_parameter,
     pe__print_bundle,
     pe__bundle_active,
     pe__bundle_resource_state,
     native_location,
     pe__free_bundle
    }
};

static enum pe_obj_types
get_resource_type(const char *name)
{
    if (safe_str_eq(name, XML_CIB_TAG_RESOURCE)) {
        return pe_native;

    } else if (safe_str_eq(name, XML_CIB_TAG_GROUP)) {
        return pe_group;

    } else if (safe_str_eq(name, XML_CIB_TAG_INCARNATION)) {
        return pe_clone;

    } else if (safe_str_eq(name, XML_CIB_TAG_MASTER)) {
        // @COMPAT deprecated since 2.0.0
        return pe_clone;

    } else if (safe_str_eq(name, XML_CIB_TAG_CONTAINER)) {
        return pe_container;
    }

    return pe_unknown;
}

static void
dup_attr(gpointer key, gpointer value, gpointer user_data)
{
    add_hash_param(user_data, key, value);
}

void
get_meta_attributes(GHashTable * meta_hash, resource_t * rsc,
                    node_t * node, pe_working_set_t * data_set)
{
    GHashTable *node_hash = NULL;

    if (node) {
        node_hash = node->details->attrs;
    }

    if (rsc->xml) {
        xmlAttrPtr xIter = NULL;

        for (xIter = rsc->xml->properties; xIter; xIter = xIter->next) {
            const char *prop_name = (const char *)xIter->name;
            const char *prop_value = crm_element_value(rsc->xml, prop_name);

            add_hash_param(meta_hash, prop_name, prop_value);
        }
    }

    unpack_instance_attributes(data_set->input, rsc->xml, XML_TAG_META_SETS, node_hash,
                               meta_hash, NULL, FALSE, data_set->now);

    /* set anything else based on the parent */
    if (rsc->parent != NULL) {
        g_hash_table_foreach(rsc->parent->meta, dup_attr, meta_hash);
    }

    /* and finally check the defaults */
    unpack_instance_attributes(data_set->input, data_set->rsc_defaults, XML_TAG_META_SETS,
                               node_hash, meta_hash, NULL, FALSE, data_set->now);
}

void
get_rsc_attributes(GHashTable * meta_hash, resource_t * rsc,
                   node_t * node, pe_working_set_t * data_set)
{
    GHashTable *node_hash = NULL;

    if (node) {
        node_hash = node->details->attrs;
    }

    unpack_instance_attributes(data_set->input, rsc->xml, XML_TAG_ATTR_SETS, node_hash,
                               meta_hash, NULL, FALSE, data_set->now);

    /* set anything else based on the parent */
    if (rsc->parent != NULL) {
        get_rsc_attributes(meta_hash, rsc->parent, node, data_set);

    } else {
        /* and finally check the defaults */
        unpack_instance_attributes(data_set->input, data_set->rsc_defaults, XML_TAG_ATTR_SETS,
                                   node_hash, meta_hash, NULL, FALSE, data_set->now);
    }
}

#if ENABLE_VERSIONED_ATTRS
void
pe_get_versioned_attributes(xmlNode * meta_hash, resource_t * rsc,
                            node_t * node, pe_working_set_t * data_set)
{
    GHashTable *node_hash = NULL;

    if (node) {
        node_hash = node->details->attrs;
    }

    pe_unpack_versioned_attributes(data_set->input, rsc->xml, XML_TAG_ATTR_SETS, node_hash,
                                   meta_hash, data_set->now);

    /* set anything else based on the parent */
    if (rsc->parent != NULL) {
        pe_get_versioned_attributes(meta_hash, rsc->parent, node, data_set);

    } else {
        /* and finally check the defaults */
        pe_unpack_versioned_attributes(data_set->input, data_set->rsc_defaults, XML_TAG_ATTR_SETS,
                                       node_hash, meta_hash, data_set->now);
    }
}
#endif

static char *
template_op_key(xmlNode * op)
{
    const char *name = crm_element_value(op, "name");
    const char *role = crm_element_value(op, "role");
    char *key = NULL;

    if (role == NULL || crm_str_eq(role, RSC_ROLE_STARTED_S, TRUE)
        || crm_str_eq(role, RSC_ROLE_SLAVE_S, TRUE)) {
        role = RSC_ROLE_UNKNOWN_S;
    }

    key = crm_concat(name, role, '-');
    return key;
}

static gboolean
unpack_template(xmlNode * xml_obj, xmlNode ** expanded_xml, pe_working_set_t * data_set)
{
    xmlNode *cib_resources = NULL;
    xmlNode *template = NULL;
    xmlNode *new_xml = NULL;
    xmlNode *child_xml = NULL;
    xmlNode *rsc_ops = NULL;
    xmlNode *template_ops = NULL;
    const char *template_ref = NULL;
    const char *clone = NULL;
    const char *id = NULL;

    if (xml_obj == NULL) {
        pe_err("No resource object for template unpacking");
        return FALSE;
    }

    template_ref = crm_element_value(xml_obj, XML_CIB_TAG_RSC_TEMPLATE);
    if (template_ref == NULL) {
        return TRUE;
    }

    id = ID(xml_obj);
    if (id == NULL) {
        pe_err("'%s' object must have a id", crm_element_name(xml_obj));
        return FALSE;
    }

    if (crm_str_eq(template_ref, id, TRUE)) {
        pe_err("The resource object '%s' should not reference itself", id);
        return FALSE;
    }

    cib_resources = get_xpath_object("//"XML_CIB_TAG_RESOURCES, data_set->input, LOG_TRACE);
    if (cib_resources == NULL) {
        pe_err("No resources configured");
        return FALSE;
    }

    template = find_entity(cib_resources, XML_CIB_TAG_RSC_TEMPLATE, template_ref);
    if (template == NULL) {
        pe_err("No template named '%s'", template_ref);
        return FALSE;
    }

    new_xml = copy_xml(template);
    xmlNodeSetName(new_xml, xml_obj->name);
    crm_xml_replace(new_xml, XML_ATTR_ID, id);

    clone = crm_element_value(xml_obj, XML_RSC_ATTR_INCARNATION);
    if(clone) {
        crm_xml_add(new_xml, XML_RSC_ATTR_INCARNATION, clone);
    }

    template_ops = find_xml_node(new_xml, "operations", FALSE);

    for (child_xml = __xml_first_child_element(xml_obj); child_xml != NULL;
         child_xml = __xml_next_element(child_xml)) {
        xmlNode *new_child = NULL;

        new_child = add_node_copy(new_xml, child_xml);

        if (crm_str_eq((const char *)new_child->name, "operations", TRUE)) {
            rsc_ops = new_child;
        }
    }

    if (template_ops && rsc_ops) {
        xmlNode *op = NULL;
        GHashTable *rsc_ops_hash = g_hash_table_new_full(crm_str_hash,
                                                         g_str_equal, free,
                                                         NULL);

        for (op = __xml_first_child_element(rsc_ops); op != NULL;
             op = __xml_next_element(op)) {

            char *key = template_op_key(op);

            g_hash_table_insert(rsc_ops_hash, key, op);
        }

        for (op = __xml_first_child_element(template_ops); op != NULL;
             op = __xml_next_element(op)) {

            char *key = template_op_key(op);

            if (g_hash_table_lookup(rsc_ops_hash, key) == NULL) {
                add_node_copy(rsc_ops, op);
            }

            free(key);
        }

        if (rsc_ops_hash) {
            g_hash_table_destroy(rsc_ops_hash);
        }

        free_xml(template_ops);
    }

    /*free_xml(*expanded_xml); */
    *expanded_xml = new_xml;

    /* Disable multi-level templates for now */
    /*if(unpack_template(new_xml, expanded_xml, data_set) == FALSE) {
       free_xml(*expanded_xml);
       *expanded_xml = NULL;

       return FALSE;
       } */

    return TRUE;
}

static gboolean
add_template_rsc(xmlNode * xml_obj, pe_working_set_t * data_set)
{
    const char *template_ref = NULL;
    const char *id = NULL;

    if (xml_obj == NULL) {
        pe_err("No resource object for processing resource list of template");
        return FALSE;
    }

    template_ref = crm_element_value(xml_obj, XML_CIB_TAG_RSC_TEMPLATE);
    if (template_ref == NULL) {
        return TRUE;
    }

    id = ID(xml_obj);
    if (id == NULL) {
        pe_err("'%s' object must have a id", crm_element_name(xml_obj));
        return FALSE;
    }

    if (crm_str_eq(template_ref, id, TRUE)) {
        pe_err("The resource object '%s' should not reference itself", id);
        return FALSE;
    }

    if (add_tag_ref(data_set->template_rsc_sets, template_ref, id) == FALSE) {
        return FALSE;
    }

    return TRUE;
}

static bool
detect_promotable(resource_t *rsc)
{
    const char *promotable = g_hash_table_lookup(rsc->meta,
                                                 XML_RSC_ATTR_PROMOTABLE);

    if (crm_is_true(promotable)) {
        return TRUE;
    }

    // @COMPAT deprecated since 2.0.0
    if (safe_str_eq(crm_element_name(rsc->xml), XML_CIB_TAG_MASTER)) {
        /* @TODO in some future version, pe_warn_once() here,
         *       then drop support in even later version
         */
        g_hash_table_insert(rsc->meta, strdup(XML_RSC_ATTR_PROMOTABLE),
                            strdup(XML_BOOLEAN_TRUE));
        return TRUE;
    }
    return FALSE;
}

gboolean
common_unpack(xmlNode * xml_obj, resource_t ** rsc,
              resource_t * parent, pe_working_set_t * data_set)
{
    bool isdefault = FALSE;
    xmlNode *expanded_xml = NULL;
    xmlNode *ops = NULL;
    const char *value = NULL;
    const char *rclass = NULL; /* Look for this after any templates have been expanded */
    const char *id = crm_element_value(xml_obj, XML_ATTR_ID);
    bool guest_node = FALSE;
    bool remote_node = FALSE;
    bool has_versioned_params = FALSE;

    crm_log_xml_trace(xml_obj, "Processing resource input...");

    if (id == NULL) {
        pe_err("Must specify id tag in <resource>");
        return FALSE;

    } else if (rsc == NULL) {
        pe_err("Nowhere to unpack resource into");
        return FALSE;

    }

    if (unpack_template(xml_obj, &expanded_xml, data_set) == FALSE) {
        return FALSE;
    }

    *rsc = calloc(1, sizeof(resource_t));
    (*rsc)->cluster = data_set;

    if (expanded_xml) {
        crm_log_xml_trace(expanded_xml, "Expanded resource...");
        (*rsc)->xml = expanded_xml;
        (*rsc)->orig_xml = xml_obj;

    } else {
        (*rsc)->xml = xml_obj;
        (*rsc)->orig_xml = NULL;
    }

    /* Do not use xml_obj from here on, use (*rsc)->xml in case templates are involved */
    rclass = crm_element_value((*rsc)->xml, XML_AGENT_ATTR_CLASS);
    (*rsc)->parent = parent;

    ops = find_xml_node((*rsc)->xml, "operations", FALSE);
    (*rsc)->ops_xml = expand_idref(ops, data_set->input);

    (*rsc)->variant = get_resource_type(crm_element_name((*rsc)->xml));
    if ((*rsc)->variant == pe_unknown) {
        pe_err("Unknown resource type: %s", crm_element_name((*rsc)->xml));
        free(*rsc);
        return FALSE;
    }

    (*rsc)->parameters = crm_str_table_new();

#if ENABLE_VERSIONED_ATTRS
    (*rsc)->versioned_parameters = create_xml_node(NULL, XML_TAG_RSC_VER_ATTRS);
#endif

    (*rsc)->meta = crm_str_table_new();

    (*rsc)->allowed_nodes =
        g_hash_table_new_full(crm_str_hash, g_str_equal, NULL, free);

    (*rsc)->known_on = g_hash_table_new_full(crm_str_hash, g_str_equal, NULL,
                                             free);

    value = crm_element_value((*rsc)->xml, XML_RSC_ATTR_INCARNATION);
    if (value) {
        (*rsc)->id = crm_concat(id, value, ':');
        add_hash_param((*rsc)->meta, XML_RSC_ATTR_INCARNATION, value);

    } else {
        (*rsc)->id = strdup(id);
    }

    (*rsc)->fns = &resource_class_functions[(*rsc)->variant];
    pe_rsc_trace((*rsc), "Unpacking resource...");

    get_meta_attributes((*rsc)->meta, *rsc, NULL, data_set);
    get_rsc_attributes((*rsc)->parameters, *rsc, NULL, data_set);
#if ENABLE_VERSIONED_ATTRS
    pe_get_versioned_attributes((*rsc)->versioned_parameters, *rsc, NULL, data_set);
#endif

    (*rsc)->flags = 0;
    set_bit((*rsc)->flags, pe_rsc_runnable);
    set_bit((*rsc)->flags, pe_rsc_provisional);

    if (is_not_set(data_set->flags, pe_flag_maintenance_mode)) {
        set_bit((*rsc)->flags, pe_rsc_managed);
    }

    (*rsc)->rsc_cons = NULL;
    (*rsc)->rsc_tickets = NULL;
    (*rsc)->actions = NULL;
    (*rsc)->role = RSC_ROLE_STOPPED;
    (*rsc)->next_role = RSC_ROLE_UNKNOWN;

    (*rsc)->recovery_type = recovery_stop_start;
    (*rsc)->stickiness = 0;
    (*rsc)->migration_threshold = INFINITY;
    (*rsc)->failure_timeout = 0;

    value = g_hash_table_lookup((*rsc)->meta, XML_CIB_ATTR_PRIORITY);
    (*rsc)->priority = crm_parse_int(value, "0");

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_NOTIFY);
    if (crm_is_true(value)) {
        set_bit((*rsc)->flags, pe_rsc_notify);
    }

    if (xml_contains_remote_node((*rsc)->xml)) {
        (*rsc)->is_remote_node = TRUE;
        if (g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_CONTAINER)) {
            guest_node = TRUE;
        } else {
            remote_node = TRUE;
        }
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_OP_ATTR_ALLOW_MIGRATE);
#if ENABLE_VERSIONED_ATTRS
    has_versioned_params = xml_has_children((*rsc)->versioned_parameters);
#endif
    if (crm_is_true(value) && has_versioned_params) {
        pe_rsc_trace((*rsc), "Migration is disabled for resources with versioned parameters");
    } else if (crm_is_true(value)) {
        set_bit((*rsc)->flags, pe_rsc_allow_migrate);
    } else if ((value == NULL) && remote_node && !has_versioned_params) {
        /* By default, we want remote nodes to be able
         * to float around the cluster without having to stop all the
         * resources within the remote-node before moving. Allowing
         * migration support enables this feature. If this ever causes
         * problems, migration support can be explicitly turned off with
         * allow-migrate=false.
         * We don't support migration for versioned resources, though. */
        set_bit((*rsc)->flags, pe_rsc_allow_migrate);
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_MANAGED);
    if (value != NULL && safe_str_neq("default", value)) {
        gboolean bool_value = TRUE;

        crm_str_to_boolean(value, &bool_value);
        if (bool_value == FALSE) {
            clear_bit((*rsc)->flags, pe_rsc_managed);
        } else {
            set_bit((*rsc)->flags, pe_rsc_managed);
        }
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_MAINTENANCE);
    if (value != NULL && safe_str_neq("default", value)) {
        gboolean bool_value = FALSE;

        crm_str_to_boolean(value, &bool_value);
        if (bool_value == TRUE) {
            clear_bit((*rsc)->flags, pe_rsc_managed);
            set_bit((*rsc)->flags, pe_rsc_maintenance);
        }

    } else if (is_set(data_set->flags, pe_flag_maintenance_mode)) {
        clear_bit((*rsc)->flags, pe_rsc_managed);
        set_bit((*rsc)->flags, pe_rsc_maintenance);
    }

    if (pe_rsc_is_clone(uber_parent(*rsc))) {
        value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_UNIQUE);
        if (crm_is_true(value)) {
            set_bit((*rsc)->flags, pe_rsc_unique);
        }
        if (detect_promotable(*rsc)) {
            set_bit((*rsc)->flags, pe_rsc_promotable);
        }
    } else {
        set_bit((*rsc)->flags, pe_rsc_unique);
    }

    pe_rsc_trace((*rsc), "Options for %s", (*rsc)->id);

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_RESTART);
    if (safe_str_eq(value, "restart")) {
        (*rsc)->restart_type = pe_restart_restart;
        pe_rsc_trace((*rsc), "\tDependency restart handling: restart");
        pe_warn_once(pe_wo_restart_type,
                     "Support for restart-type is deprecated and will be removed in a future release");

    } else {
        (*rsc)->restart_type = pe_restart_ignore;
        pe_rsc_trace((*rsc), "\tDependency restart handling: ignore");
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_MULTIPLE);
    if (safe_str_eq(value, "stop_only")) {
        (*rsc)->recovery_type = recovery_stop_only;
        pe_rsc_trace((*rsc), "\tMultiple running resource recovery: stop only");

    } else if (safe_str_eq(value, "block")) {
        (*rsc)->recovery_type = recovery_block;
        pe_rsc_trace((*rsc), "\tMultiple running resource recovery: block");

    } else {
        (*rsc)->recovery_type = recovery_stop_start;
        pe_rsc_trace((*rsc), "\tMultiple running resource recovery: stop/start");
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_STICKINESS);
    if (value != NULL && safe_str_neq("default", value)) {
        (*rsc)->stickiness = char2score(value);
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_FAIL_STICKINESS);
    if (value != NULL && safe_str_neq("default", value)) {
        (*rsc)->migration_threshold = char2score(value);
    }

    if (safe_str_eq(rclass, PCMK_RESOURCE_CLASS_STONITH)) {
        set_bit(data_set->flags, pe_flag_have_stonith_resource);
        set_bit((*rsc)->flags, pe_rsc_fence_device);
    }

    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_REQUIRES);

  handle_requires_pref:
    if (safe_str_eq(value, "nothing")) {

    } else if (safe_str_eq(value, "quorum")) {
        set_bit((*rsc)->flags, pe_rsc_needs_quorum);

    } else if (safe_str_eq(value, "unfencing")) {
        if (is_set((*rsc)->flags, pe_rsc_fence_device)) {
            crm_config_warn("%s is a fencing device but requires (un)fencing", (*rsc)->id);
            value = "quorum";
            isdefault = TRUE;
            goto handle_requires_pref;

        } else if (is_not_set(data_set->flags, pe_flag_stonith_enabled)) {
            crm_config_warn("%s requires (un)fencing but fencing is disabled", (*rsc)->id);
            value = "quorum";
            isdefault = TRUE;
            goto handle_requires_pref;

        } else {
            set_bit((*rsc)->flags, pe_rsc_needs_fencing);
            set_bit((*rsc)->flags, pe_rsc_needs_unfencing);
        }

    } else if (safe_str_eq(value, "fencing")) {
        set_bit((*rsc)->flags, pe_rsc_needs_fencing);
        if (is_not_set(data_set->flags, pe_flag_stonith_enabled)) {
            crm_config_warn("%s requires fencing but fencing is disabled", (*rsc)->id);
        }

    } else {
        if (value) {
            crm_config_err("Invalid value for %s->requires: %s%s",
                           (*rsc)->id, value,
                           is_set(data_set->flags, pe_flag_stonith_enabled) ? "" : " (stonith-enabled=false)");
        }

        isdefault = TRUE;
        if(is_set((*rsc)->flags, pe_rsc_fence_device)) {
            value = "quorum";

        } else if (((*rsc)->variant == pe_native)
                   && safe_str_eq(crm_element_value((*rsc)->xml, XML_AGENT_ATTR_CLASS),
                                  PCMK_RESOURCE_CLASS_OCF)
                   && safe_str_eq(crm_element_value((*rsc)->xml, XML_AGENT_ATTR_PROVIDER), "pacemaker")
                   && safe_str_eq(crm_element_value((*rsc)->xml, XML_ATTR_TYPE), "remote")
            ) {
            value = "quorum";

        } else if (is_set(data_set->flags, pe_flag_enable_unfencing)) {
            value = "unfencing";

        } else if (is_set(data_set->flags, pe_flag_stonith_enabled)) {
            value = "fencing";

        } else if (data_set->no_quorum_policy == no_quorum_ignore) {
            value = "nothing";

        } else {
            value = "quorum";
        }
        goto handle_requires_pref;
    }

    pe_rsc_trace((*rsc), "\tRequired to start: %s%s", value, isdefault?" (default)":"");
    value = g_hash_table_lookup((*rsc)->meta, XML_RSC_ATTR_FAIL_TIMEOUT);
    if (value != NULL) {
        /* call crm_get_msec() and convert back to seconds */
        (*rsc)->failure_timeout = (crm_get_msec(value) / 1000);
    }

    if (remote_node) {
        value = g_hash_table_lookup((*rsc)->parameters, XML_REMOTE_ATTR_RECONNECT_INTERVAL);
        if (value) {
            /* reconnect delay works by setting failure_timeout and preventing the
             * connection from starting until the failure is cleared. */
            (*rsc)->remote_reconnect_ms = crm_parse_interval_spec(value);
            /* we want to override any default failure_timeout in use when remote
             * reconnect_interval is in use. */ 
            (*rsc)->failure_timeout = (*rsc)->remote_reconnect_ms / 1000;
        }
    }

    get_target_role(*rsc, &((*rsc)->next_role));
    pe_rsc_trace((*rsc), "\tDesired next state: %s",
                 (*rsc)->next_role != RSC_ROLE_UNKNOWN ? role2text((*rsc)->next_role) : "default");

    if ((*rsc)->fns->unpack(*rsc, data_set) == FALSE) {
        return FALSE;
    }

    if (is_set(data_set->flags, pe_flag_symmetric_cluster)) {
        // This tag must stay exactly the same because it is tested elsewhere
        resource_location(*rsc, NULL, 0, "symmetric_default", data_set);
    } else if (guest_node) {
        /* remote resources tied to a container resource must always be allowed
         * to opt-in to the cluster. Whether the connection resource is actually
         * allowed to be placed on a node is dependent on the container resource */
        resource_location(*rsc, NULL, 0, "remote_connection_default", data_set);
    }

    pe_rsc_trace((*rsc), "\tAction notification: %s",
                 is_set((*rsc)->flags, pe_rsc_notify) ? "required" : "not required");

    (*rsc)->utilization = crm_str_table_new();

    unpack_instance_attributes(data_set->input, (*rsc)->xml, XML_TAG_UTILIZATION, NULL,
                               (*rsc)->utilization, NULL, FALSE, data_set->now);

/* 	data_set->resources = g_list_append(data_set->resources, (*rsc)); */

    if (expanded_xml) {
        if (add_template_rsc(xml_obj, data_set) == FALSE) {
            return FALSE;
        }
    }
    return TRUE;
}

void
common_update_score(resource_t * rsc, const char *id, int score)
{
    node_t *node = NULL;

    node = pe_hash_table_lookup(rsc->allowed_nodes, id);
    if (node != NULL) {
        pe_rsc_trace(rsc, "Updating score for %s on %s: %d + %d", rsc->id, id, node->weight, score);
        node->weight = merge_weights(node->weight, score);
    }

    if (rsc->children) {
        GListPtr gIter = rsc->children;

        for (; gIter != NULL; gIter = gIter->next) {
            resource_t *child_rsc = (resource_t *) gIter->data;

            common_update_score(child_rsc, id, score);
        }
    }
}

gboolean
is_parent(resource_t *child, resource_t *rsc)
{
    resource_t *parent = child;

    if (parent == NULL || rsc == NULL) {
        return FALSE;
    }
    while (parent->parent != NULL) {
        if (parent->parent == rsc) {
            return TRUE;
        }
        parent = parent->parent;
    }
    return FALSE;
}

resource_t *
uber_parent(resource_t * rsc)
{
    resource_t *parent = rsc;

    if (parent == NULL) {
        return NULL;
    }
    while (parent->parent != NULL && parent->parent->variant != pe_container) {
        parent = parent->parent;
    }
    return parent;
}

void
common_free(resource_t * rsc)
{
    if (rsc == NULL) {
        return;
    }

    pe_rsc_trace(rsc, "Freeing %s %d", rsc->id, rsc->variant);

    g_list_free(rsc->rsc_cons);
    g_list_free(rsc->rsc_cons_lhs);
    g_list_free(rsc->rsc_tickets);
    g_list_free(rsc->dangling_migrations);

    if (rsc->parameters != NULL) {
        g_hash_table_destroy(rsc->parameters);
    }
#if ENABLE_VERSIONED_ATTRS
    if (rsc->versioned_parameters != NULL) {
        free_xml(rsc->versioned_parameters);
    }
#endif
    if (rsc->meta != NULL) {
        g_hash_table_destroy(rsc->meta);
    }
    if (rsc->utilization != NULL) {
        g_hash_table_destroy(rsc->utilization);
    }

    if (rsc->parent == NULL && is_set(rsc->flags, pe_rsc_orphan)) {
        free_xml(rsc->xml);
        rsc->xml = NULL;
        free_xml(rsc->orig_xml);
        rsc->orig_xml = NULL;

        /* if rsc->orig_xml, then rsc->xml is an expanded xml from a template */
    } else if (rsc->orig_xml) {
        free_xml(rsc->xml);
        rsc->xml = NULL;
    }
    if (rsc->running_on) {
        g_list_free(rsc->running_on);
        rsc->running_on = NULL;
    }
    if (rsc->known_on) {
        g_hash_table_destroy(rsc->known_on);
        rsc->known_on = NULL;
    }
    if (rsc->actions) {
        g_list_free(rsc->actions);
        rsc->actions = NULL;
    }
    if (rsc->allowed_nodes) {
        g_hash_table_destroy(rsc->allowed_nodes);
        rsc->allowed_nodes = NULL;
    }
    g_list_free(rsc->fillers);
    g_list_free(rsc->rsc_location);
    pe_rsc_trace(rsc, "Resource freed");
    free(rsc->id);
    free(rsc->clone_name);
    free(rsc->allocated_to);
    free(rsc->variant_opaque);
    free(rsc->pending_task);
    free(rsc);
}

/*!
 * \brief
 * \internal Find a node (and optionally count all) where resource is active
 *
 * \param[in]  rsc          Resource to check
 * \param[out] count_all    If not NULL, will be set to count of active nodes
 * \param[out] count_clean  If not NULL, will be set to count of clean nodes
 *
 * \return An active node (or NULL if resource is not active anywhere)
 *
 * \note The order of preference is: an active node that is the resource's
 *       partial migration source; if the resource's "requires" is "quorum" or
 *       "nothing", the first active node in the list that is clean and online;
 *       the first active node in the list.
 */
pe_node_t *
pe__find_active_on(const pe_resource_t *rsc, unsigned int *count_all,
                   unsigned int *count_clean)
{
    pe_node_t *active = NULL;
    pe_node_t *node = NULL;
    bool keep_looking = FALSE;
    bool is_happy = FALSE;

    if (count_all) {
        *count_all = 0;
    }
    if (count_clean) {
        *count_clean = 0;
    }
    if (rsc == NULL) {
        return NULL;
    }

    for (GList *node_iter = rsc->running_on; node_iter != NULL;
         node_iter = node_iter->next) {

        node = node_iter->data;
        keep_looking = FALSE;

        is_happy = node->details->online && !node->details->unclean;

        if (count_all) {
            ++*count_all;
        }
        if (count_clean && is_happy) {
            ++*count_clean;
        }
        if (count_all || count_clean) {
            // If we're counting, we need to go through entire list
            keep_looking = TRUE;
        }

        if (rsc->partial_migration_source != NULL) {
            if (node->details == rsc->partial_migration_source->details) {
                // This is the migration source
                active = node;
            } else {
                keep_looking = TRUE;
            }
        } else if (is_not_set(rsc->flags, pe_rsc_needs_fencing)) {
            if (is_happy && (!active || !active->details->online
                             || active->details->unclean)) {
                // This is the first clean node
                active = node;
            } else {
                keep_looking = TRUE;
            }
        }
        if (active == NULL) {
            // This is first node in list
            active = node;
        }

        if (keep_looking == FALSE) {
            // Don't waste time iterating if we don't have to
            break;
        }
    }
    return active;
}

/*!
 * \brief
 * \internal Find and count active nodes according to "requires"
 *
 * \param[in]  rsc    Resource to check
 * \param[out] count  If not NULL, will be set to count of active nodes
 *
 * \return An active node (or NULL if resource is not active anywhere)
 *
 * \note This is a convenience wrapper for pe__find_active_on() where the count
 *       of all active nodes or only clean active nodes is desired according to
 *       the "requires" meta-attribute.
 */
pe_node_t *
pe__find_active_requires(const pe_resource_t *rsc, unsigned int *count)
{
    if (rsc && is_not_set(rsc->flags, pe_rsc_needs_fencing)) {
        return pe__find_active_on(rsc, NULL, count);
    }
    return pe__find_active_on(rsc, count, NULL);
}
