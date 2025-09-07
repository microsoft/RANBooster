# Introduction

Reference middlebox implementations for enhancing the fronthaul capabilities of Open RAN DU network functions.

The repository currently provides the following implementations:

* Distributed Antenna System (DPDK and XDP implementation)
* Distributed MIMO (DPDK and XDP implementation)
* Real-time PRB monitoring (DPDK and XDP implementation)
* RU sharing (DPDK implementation)

# Build instructions

The simplest way to build the middleboxes is in the form of a container image, using the provided Dockerfile from the project's root:

```bash
docker build -f deploy/ubuntu24_04.Dockerfile -t ranbooster .
```

To build manually, you can use the [provided Dockerfile](./deploy/ubuntu24_04.Dockerfile) as a documentation of the project's dependencies and build instructions. 

# Tested RAN components

The middleboxes have been verified with the following RAN stacks and O-RAN RUs:

* srsRAN DU
* CapGemini DU
* Radisys DU
* Foxconn RPQN 7800 O-RU

# Supported NICs

The middleboxes have been tested with the following NICs:

* Mellanox ConnectX-6 Dx

**Trademarks** This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft’s Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general). Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship. Any use of third-party trademarks or logos are subject to those third-party’s policies.