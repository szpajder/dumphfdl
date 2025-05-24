/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>                      // fprintf
#include <string.h>                     // strdup, strerror
#include <librdkafka/rdkafka.h>
#include "output-common.h"              // output_descriptor_t, output_qentry_t, output_queue_drain
#include "kvargs.h"                     // kvargs
#include "options.h"                    // option_descr_t
#include "util.h"                       // ASSERT, NEW

typedef struct {
    char *brokers;
    char *topic;
    char *sasl_username;
    char *sasl_password;
    char *sasl_mechanism;
    char *security_protocol;
    char *acks;
    void *rk;
    int kafka_metadata_timeout_ms;
    char *ssl_ca_location;
} out_rdkafka_ctx_t;

static bool out_rdkafka_supports_format(output_format_t format) {
    return(format == OFMT_TEXT || format == OFMT_BASESTATION || format == OFMT_JSON);
}

static void *out_rdkafka_configure(kvargs *kv) {
    ASSERT(kv != NULL);
    NEW(out_rdkafka_ctx_t, cfg);

    if(kvargs_get(kv, "brokers") == NULL) {
        fprintf(stderr, "output_rdkafka: brokers not specified\n");
        goto fail;
    }
    cfg->brokers = strdup(kvargs_get(kv, "brokers"));
    fprintf(stderr, "output_rdkafka: Configuring broker: %s\n", cfg->brokers);

    if(kvargs_get(kv, "topic") == NULL) {
        fprintf(stderr, "output_rdkafka: topic not specified\n");
        goto fail;
    }
    cfg->topic = strdup(kvargs_get(kv, "topic"));

    if(kvargs_get(kv, "sasl_username") != NULL &&
       kvargs_get(kv, "sasl_password") != NULL &&
       kvargs_get(kv, "sasl_mechanism") != NULL &&
       kvargs_get(kv, "security_protocol") != NULL) {
       // Enable SASL authentication
      cfg->sasl_username = strdup(kvargs_get(kv, "sasl_username"));
      cfg->sasl_password = strdup(kvargs_get(kv, "sasl_password"));
      cfg->sasl_mechanism = strdup(kvargs_get(kv, "sasl_mechanism"));
      cfg->security_protocol = strdup(kvargs_get(kv, "security_protocol"));
    }

    if (kvargs_get(kv, "ssl_ca_location") != NULL) {
      cfg->ssl_ca_location = strdup(kvargs_get(kv, "ssl_ca_location"));
      fprintf(stderr, "output_rdkafka: Configuring SSL CA certificate: %s\n", cfg->ssl_ca_location);
    }

    if (kvargs_get(kv, "acks") != NULL) {
      cfg->acks = strdup(kvargs_get(kv, "acks"));
    } else {
      cfg->acks = "all";
    }

    if (kvargs_get(kv, "kafka_connect_timeout_secs") != NULL) {
      cfg->kafka_metadata_timeout_ms = atoi(strdup(kvargs_get(kv, "kafka_connect_timeout_secs"))) * 1000;
    } else {
      // Set default metadata query timeout to 10 seconds.
      cfg->kafka_metadata_timeout_ms = 10 * 1000;
    }

    return cfg;
fail:
    XFREE(cfg);
    return NULL;
}

static int rdkafka_conf_set(rd_kafka_conf_t *conf, char *key, char *val) {
  ASSERT(conf != NULL);

  char errstr[512];
  if (rd_kafka_conf_set(conf, key, val,
                        errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    fprintf(stderr, "%% rdkafka config error: %s\n", errstr);
    return -1;
  }

  return 0;
}

// Delivery report callback handler
static void rdkafka_delivery_report_cb(rd_kafka_t *rk,
                      const rd_kafka_message_t *rkmessage,
                      void *opaque) {
    UNUSED(rk);
    out_rdkafka_ctx_t *self = opaque;

    if (rkmessage->err) {
        fprintf(stderr, "output_rdkafka(%s): ERROR: message delivery failed: %s\n",
            self->brokers,
            rd_kafka_err2str(rkmessage->err));
    }
}

// Error callback handler
static void rdkafka_error_cb(rd_kafka_t *rk, int err, const char *reason, void *opaque) {
    out_rdkafka_ctx_t *self = opaque;

    // Determine if the error is fatal, or retryable, and notify user accordingly.
    char errstr[512];
    rd_kafka_resp_err_t fatal_error = rd_kafka_fatal_error(rk, errstr, sizeof(errstr));

    if (fatal_error != RD_KAFKA_RESP_ERR_NO_ERROR) {
        fprintf(stderr, "output_rdkafka(%s): FATAL ERROR (%d: %s): %s\n",
                self->brokers, fatal_error, rd_kafka_err2name(fatal_error), errstr);
    } else {
        // (probable) Transient error
        fprintf(stderr, "output_rdkafka(%s) warning: (%d: %s): %s\n",
                self->brokers, err, rd_kafka_err2name(err), reason);
    }
}

static int out_rdkafka_init(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;

    rd_kafka_conf_t *conf = rd_kafka_conf_new();

    // Register an error callback handler.
    rd_kafka_conf_set_error_cb(conf, rdkafka_error_cb);

    // Set delivery report callback
    rd_kafka_conf_set_dr_msg_cb(conf, rdkafka_delivery_report_cb);

    // Pass in pointer to our config struct so we can use some values for logging.
    rd_kafka_conf_set_opaque(conf, self);

    char errstr[512];

    if (rdkafka_conf_set(conf, "bootstrap.servers", self->brokers) < 0) { return -1; }
    if (rdkafka_conf_set(conf, "acks", self->acks) < 0) { return -1; }

    // Optionally configure a custom SSL CA certificate to verify the servers certificate
    // against. If this file path is wrong or inaccessible, librdkafka will return an error.
    if (self->ssl_ca_location != NULL) {
      if (rdkafka_conf_set(conf, "ssl.ca.location", self->ssl_ca_location) < 0) { return -1; }
    }

    // Enable authentication if configured
    if (self->sasl_username != NULL &&
        self->sasl_password != NULL &&
        self->sasl_mechanism != NULL &&
        self->security_protocol != NULL) {
          if (rdkafka_conf_set(conf, "sasl.mechanism", self->sasl_mechanism) < 0) { return -1; }
          if (rdkafka_conf_set(conf, "sasl.username", self->sasl_username) < 0) { return -1; }
          if (rdkafka_conf_set(conf, "sasl.password", self->sasl_password) < 0) { return -1; }
          if (rdkafka_conf_set(conf, "security.protocol", self->security_protocol) < 0) { return -1; }
    }

    /* Create Kafka producer handle */
	  fprintf(stderr, "output_rdkafka(%s): creating producer...\n", self->brokers);
    if (!(self->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf,
                            errstr, sizeof(errstr)))) {
      fprintf(stderr, "%% Failed to create new producer: %s\n", errstr);
      return -1;
    }

    const struct rd_kafka_metadata *metadata;
    rd_kafka_resp_err_t err = rd_kafka_metadata(self->rk, 0, NULL, &metadata, self->kafka_metadata_timeout_ms);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        fprintf(stderr, "output_rdkafka(%s): failed to fetch metadata - check Kafka configuration: %s\n",
            self->brokers, rd_kafka_err2str(err));
        rd_kafka_destroy(self->rk);
        return -1;
    } else {
	      fprintf(stderr, "output_rdkafka(%s): connection established\n", self->brokers);
    }
    rd_kafka_metadata_destroy(metadata);

    return 0;
}

static int out_rdkafka_produce_text(out_rdkafka_ctx_t *self, struct metadata *metadata, struct octet_string *msg) {
    UNUSED(metadata);
    ASSERT(msg != NULL);

    long delivery_counter;

    if(msg->len < 2) {
        return 0;
    }

    rd_kafka_resp_err_t err = rd_kafka_producev(
        self->rk,
        RD_KAFKA_V_TOPIC(self->topic),
        RD_KAFKA_V_KEY(NULL, 0),
        RD_KAFKA_V_VALUE(msg->buf, msg->len),
        RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_OPAQUE(&delivery_counter),
        RD_KAFKA_V_END);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        fprintf(stderr, "output_rdkafka(%s): Produce message failed: %s\n",
                self->brokers, rd_kafka_err2str(err));
        return -1;
    }

    /* Poll for delivery report callbacks to know the final
     * delivery status of previously produced messages. */
    rd_kafka_poll(self->rk, 0);

    return 0;
}

static int out_rdkafka_produce(void *selfptr, output_format_t format, struct metadata *metadata, struct octet_string *msg) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    if(format == OFMT_TEXT || format == OFMT_JSON || format == OFMT_BASESTATION) {
        if (out_rdkafka_produce_text(self, metadata, msg) < 0) {
          return -1;
        }
    }
    return 0;
}

static void out_rdkafka_handle_shutdown(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    fprintf(stderr, "output_rdkafka(%s): shutting down\n", self->brokers);
    rd_kafka_destroy(self->rk);
}

static void out_rdkafka_handle_failure(void *selfptr) {
    ASSERT(selfptr != NULL);
    out_rdkafka_ctx_t *self = selfptr;
    fprintf(stderr, "output_rdkafka(%s): deactivating output\n",
            self->brokers);
}

static const option_descr_t out_rdkafka_options[] = {
    {
        .name= "brokers",
        .description = "Kafka Brokers (broker0:9092,broker1:9092,...) (required)"
    },
    {
        .name= "topic",
        .description = "Kafka topic (required)"
    },
    {
        .name= "sasl_username",
        .description = "SASL Username"
    },
    {
        .name= "sasl_password",
        .description = "SASL Password"
    },
    {
        .name= "sasl_mechanism",
        .description = "SASL Mechanism - Accepted values: PLAIN, SCRAM-SHA-256, SCRAM-SHA-512"
    },
    {
        .name= "security_protocol",
        .description = "Security Protocol - Accepted values: plaintext, ssl, sasl_plaintext, sasl_ssl"
    },
    {
        .name= "ssl_ca_location",
        .description = "SSL CA certificate PEM file path (if not specified, uses the system root CA pack)"
    },
    {
        .name= "acks",
        .description = "Required number of acks - Default: all"
    },
    {
        .name= "kafka_connect_timeout_secs",
        .description = "Seconds to wait for metadata query on connect - Default: 10 (seconds)"
    },
    {
        .name = NULL,
        .description = NULL
    }
};

output_descriptor_t out_DEF_rdkafka = {
    .name = "rdkafka",
    .description = "Output to an Apache Kafka broker",
    .options = out_rdkafka_options,
    .supports_format = out_rdkafka_supports_format,
    .configure = out_rdkafka_configure,
    .init = out_rdkafka_init,
    .produce = out_rdkafka_produce,
    .handle_shutdown = out_rdkafka_handle_shutdown,
    .handle_failure = out_rdkafka_handle_failure
};
