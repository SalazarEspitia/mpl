# The MPL builds this pack uses: a short name, the branch on the fork, and the exact commit.
# Sourced by build.sh and lib.sh; the workflow's build matrix repeats the names.
EV_REPO=${MPL_REPO:-https://github.com/SalazarEspitia/mpl}

# name  branch                  commit
EV_REFS="
base    cc261-base              5336a5cc46110cb6523b87bb2c5856fd5165d7cc
weak    cc-suspects-weakcells   c08f29257f96ddf372c922937de3727da9dfad7d
hint    heapaddr-hint           757d675aa961e69ae45e163c7ab91a9ecdc3c926
main    main                    73b576a619fb7ac725c56622e3b0cb3012a168d2
ebr     gc-ebr-align            fb5828e01964a14c8d05b967c7e074d761db9377
weak0   cc-suspects-weakcells~1 f57e866d8ae6ab2a5e02ea46c17a2ab25d4adc5e
"

ref_commit() { echo "$EV_REFS" | awk -v n="$1" '$1 == n { print $3 }'; }
ref_branch() { echo "$EV_REFS" | awk -v n="$1" '$1 == n { print $2 }'; }
