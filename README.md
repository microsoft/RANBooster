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

# Additional documentation

For more details about the project, you can check our [technical report](https://www.microsoft.com/en-us/research/publication/ranbooster-democratizing-advanced-cellular-connectivity-through-fronthaul-middleboxes/) that has been published at ACM SIGCOMM'25:

```bibtex
@inproceedings{ranbooster_sigcomm25,
  title={{RANBooster: Democratizing advanced cellular connectivity through fronthaul middleboxes}},
  author={Foukas, Xenofon and Ukyab, Tenzin Samten and Radunovic, Bozidar and Ratnasamy, Sylvia and Shenker, Scott},
  booktitle={Proceedings of the ACM SIGCOMM 2025 Conference},
  pages={742--757},
  year={2025}
}
```



**Trademarks** This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft’s Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general). Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship. Any use of third-party trademarks or logos are subject to those third-party’s policies.