

sta::define_cmd_args "placer3d_run" {
    -argc argcount
}
proc placer3d_run {args} {
    sta::parse_key_args "placer3d_run" args \
    keys {-argc} \
    flags {}

    set argcount $keys(-argc)
    gpl3d::placer3d_run $argcount
}