# Introduction

Reference middlebox implementations for enhancing the fronthaul capabilities of Open RAN DU network functions.

The repository currently provides the following implementations:

* Distributed Antenna System (DPDK and XDP implementation)
* Distributed MIMO (DPDK and XDP implementation)
* Real-time PRB monitoring (DPDK and XDP implementation)
* RU sharing (DPDK implementation)

# Build instructions

Here we present instructions for building the middleboxes in Ubuntu 24.04.
The guide uses the variable `$RANBOOSTER_SRC_PATH` to refer to the root directory of the repo.

*Note:* The guide assumes the use of a Mellanox ConnectX-6 Dx NIC. For a different NIC, you might need to adjust the dependencies accordingly.

First, we install the system dependencies:

```bash
sudo apt install -y wget cmake clang gcc g++ git libnuma-dev python3-pyelftools \
            meson ninja-build pkg-config libbpf-dev libelf-dev iproute2 \
            linux-tools-common linux-tools-generic libxdp-dev libibverbs-dev \
            ibverbs-providers libibverbs1 rdma-core
```

Next, we build DPDK. The code has been tested with DPDK 24.11.3 LTS:

```bash
mkdir -p $RANBOOSTER_SRC_PATH/dpdk-ranbooster
wget -O - https://fast.dpdk.org/rel/dpdk-24.11.3.tar.xz | tar -xJ -C $RANBOOSTER_SRC_PATH/dpdk-ranbooster --strip-components=1
cd $RANBOOSTER_SRC_PATH/dpdk-ranbooster
meson build
cd build && ninja
```

We then prepare the code, setup the environment and build the code:
```bash
cd $RANBOOSTER_SRC_PATH
source setup_ranbooster_env.sh
./init_and_patch_submodules.sh
export RTE_SDK=$RANBOOSTER_SRC_PATH/dpdk-ranbooster
export PKG_CONFIG_PATH=$RANBOOSTER_SRC_PATH/dpdk-ranbooster/build/meson-uninstalled
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j
```

# Supported Hardware

The middleboxes assume support for SIMD instructions and have been tested with the following hardware:

* CPU: Intel Xeon Gold 6338N
* NIC: Mellanox ConnectX-6 Dx

# Tested RAN components

The middleboxes have been verified with the following RAN stacks and O-RAN RUs:

* srsRAN DU
* CapGemini DU
* Radisys DU
* Foxconn RPQN 7800 O-RU

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