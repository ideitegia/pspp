static struct Kmeans *kmeans_create (struct casereader *cs,
				     const struct variable **variables,
				     int m, int ngroups, int maxiter);

static void kmeans_randomize_centers (struct Kmeans *kmeans);

static int kmeans_get_nearest_group (struct Kmeans *kmeans, struct ccase *c);

static void kmeans_recalculate_centers (struct Kmeans *kmeans);

static int
kmeans_calculate_indexes_and_check_convergence (struct Kmeans *kmeans);

static void kmeans_order_groups (struct Kmeans *kmeans);

static void kmeans_cluster (struct Kmeans *kmeans);

static void quick_cluster_show_centers (struct Kmeans *kmeans, bool initial);

static void quick_cluster_show_number_cases (struct Kmeans *kmeans);

static void quick_cluster_show_results (struct Kmeans *kmeans);

int cmd_quick_cluster (struct lexer *lexer, struct dataset *ds);

static void kmeans_destroy (struct Kmeans *kmeans);
