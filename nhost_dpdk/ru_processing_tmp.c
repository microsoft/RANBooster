
            // Check if this is coming from the RU
            if (rte_is_same_ether_addr(&config->ru_config.ru_addr, &eth_hdr->src_addr)) {

                // Check if this was going to the middlebox
                if (rte_is_same_ether_addr(&config->middlebox_addr, &eth_hdr->dst_addr)) {

                    if (ru_port_id >= MAX_PDSCH_PUSCH_PORT) { // PRACH

                        // assert(apphdr_copy.numOfSections == config->num_dus);

                        int section_offset = sizeof(struct rte_ether_hdr) + \
                                        sizeof(struct xran_ecpri_hdr) + \
                                        sizeof(struct radio_app_common_hdr);

                        // Send each section of the packet to the correct DU
                        // for (int sect_idx = 0; sect_idx < config->num_dus; sect_idx++) {

                            for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                                struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                                    section_offset);
                                
                                struct data_section_hdr data_hdr_copy = *data_hdr;
                                data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                                assert(data_hdr_copy.fields.num_prbu == 12);

                                // if (data_hdr_copy.fields.sect_id == du_idx) {

                                    // New packet
                                    struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                                    if (new_buf == NULL) {
                                        printf("Couldn't allocate new mbuf uplink uplane\n");
                                        exit(-1);
                                    }
                                    // new_buf->pkt_len = IQ_OFFSET + (12 * PRB_16_SIZE);
                                    // new_buf->data_len = IQ_OFFSET + (12 * PRB_16_SIZE);

                                    // // Check compression
                                    // struct data_section_compression_hdr *comp_hdr;
                                    // comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                                    // if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_UNCOMPRESSED) {
                                    //     printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                    //     exit(-1);
                                    // }

                                    // // Copy section
                                    // section_offset += sizeof(struct data_section_hdr) + sizeof(struct data_section_compression_hdr);

                                    // uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, section_offset);
                                    // uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                                    // memcpy(new_prbs, old_prbs, 12 * PRB_16_SIZE);

                                    // section_offset += 12 * PRB_16_SIZE;

                                    // Fix ethernet header to forward to DU
                                    rte_ether_addr_copy(&config->du_configs[du_idx].du_addr, &eth_hdr->dst_addr);
                                    rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                                    eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                                    if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                        buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                        buf->vlan_tci = config->du_configs[du_idx].vlan;
                                    }

                                    // Send new packet
                                    mx_tx_bufs[mx_tx_idx++] = new_buf;
                                // }
                            }
                        // }

                        // Free old packet
                        rte_pktmbuf_free(buf);  

                    } else { // Data 

                        struct data_section_hdr *data_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_hdr *,
                        sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) + sizeof(struct radio_app_common_hdr));
                        
                        struct data_section_hdr data_hdr_copy = *data_hdr;
                        data_hdr_copy.fields.all_bits  = rte_be_to_cpu_32(data_hdr->fields.all_bits);

                        if (data_hdr_copy.fields.num_prbu != 0) {
                            printf("Uplink data num prbu is not expected %d\n", data_hdr_copy.fields.num_prbu);
                            exit(-1);
                        }

                        // For each DU sent out a packet
                        for (int du_idx = 0; du_idx < config->num_dus; du_idx++) {

                            // Check if received a CPlane packet
                            if (cached_cplane_packets_check[du_idx][ru_port_id][symbol][subframe][slot] == 0) {
                                continue;
                            } else {
                                // Reset cache
                                cached_cplane_packets_check[du_idx][ru_port_id][symbol][subframe][slot] = 0;
                                cached_cplane_packets_num[ru_port_id][symbol][subframe][slot]--;
                            }

                            // New packet
                            struct rte_mbuf *new_buf = rte_pktmbuf_copy(buf, config->mbuf_pool, 0, UINT32_MAX);
                            if (new_buf == NULL) {
                                printf("Couldn't allocate new mbuf uplink uplane\n");
                                exit(-1);
                            }

                            // Check compression
                            struct data_section_compression_hdr *comp_hdr;
                            comp_hdr = rte_pktmbuf_mtod_offset(buf, struct data_section_compression_hdr *, HDR_SIZE);

                            if (comp_hdr->ud_comp_hdr.ud_iq_width != IQ_BIT_WIDTH_COMPRESSED) {
                                printf("IQ bit width is %d uplink\n", comp_hdr->ud_comp_hdr.ud_iq_width);
                                exit(-1);
                            }
                                
                            new_buf->pkt_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;
                            new_buf->data_len = IQ_OFFSET + config->du_prb_size * PRB_9_SIZE;

                            if (config->du_prb_size == 12) {
                                assert(new_buf->pkt_len == 3000 && new_buf->data_len == 3000);
                            }

                            // Update new num prb
                            uint32_t old_num_prb = 273;
                            uint32_t new_num_prb = config->du_prb_size;
                            struct data_section_hdr *new_data_hdr = rte_pktmbuf_mtod_offset(new_buf, struct data_section_hdr *, 
                                                sizeof(struct rte_ether_hdr) + sizeof(struct xran_ecpri_hdr) +
                                                sizeof(struct radio_app_common_hdr));
                            data_hdr_copy.fields.num_prbu = XRAN_CONVERT_NUMPRB(new_num_prb);
                            new_data_hdr->fields.all_bits = rte_be_to_cpu_32(data_hdr_copy.fields.all_bits);

                            // Copy prbs
                            int offset = config->du_configs[du_idx].prb_offset * PRB_9_SIZE;

                            uint8_t *old_prbs = rte_pktmbuf_mtod_offset(buf, uint8_t *, IQ_OFFSET);
                            uint8_t *new_prbs = rte_pktmbuf_mtod_offset(new_buf, uint8_t *, IQ_OFFSET);

                            if (old_num_prb <= new_num_prb) {
                                printf("Uplink new and old num prb not correct new = %d old = %d\n", new_num_prb, old_num_prb);
                                exit(-1);
                            }

                            // Check if there is enough data
                            if (buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                                printf("Downlink not enough data to copy from old pkt: pkt_len %d hdr_size %ld to copy size %ld\n", buf->pkt_len, IQ_OFFSET, new_num_prb * PRB_9_SIZE);
                                exit(-1);
                            }
                            if (new_buf->pkt_len - IQ_OFFSET < new_num_prb * PRB_9_SIZE) {
                                printf("Downlink not enough data to copy into new pkt: pkt_len %d hdr_size %ld offset %d to copy size %ld\n", new_buf->pkt_len, IQ_OFFSET, offset, new_num_prb * PRB_9_SIZE);
                                exit(-1);
                            }

                            memset(new_prbs, 0, new_num_prb * PRB_9_SIZE);
                            memcpy(new_prbs, old_prbs + offset, new_num_prb * PRB_9_SIZE);

                            // Fix ethernet header to forward to DU
                            rte_ether_addr_copy(&config->du_configs[du_idx].du_addr, &eth_hdr->dst_addr);
                            rte_ether_addr_copy(&config->middlebox_addr, &eth_hdr->src_addr);

                            eth_hdr->ether_type = rte_be_to_cpu_16(RTE_ETHER_TYPE_ECPRI);

                            if (buf->ol_flags & RTE_MBUF_F_RX_VLAN) {
                                buf->ol_flags |= RTE_MBUF_F_TX_VLAN;
                                buf->vlan_tci = config->du_configs[du_idx].vlan;
                            }

                            // Send new packet
                            mx_tx_bufs[mx_tx_idx++] = new_buf;

                        }

                        // Free old packet
                        rte_pktmbuf_free(buf);

                    }
                    
                } else { // If this was not going to the middlebox, ignore it
                    rte_pktmbuf_free(buf);
                    continue;
                }
            
            } else { // If this is not coming from the RU or DU, ignore it
                rte_pktmbuf_free(buf);
                continue;
            }