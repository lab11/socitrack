import tottagProcessing

log_filepath = tottagProcessing.parse_args()[0]
log_data = tottagProcessing.load_log(log_filepath)
tottagProcessing.plot(log_data)