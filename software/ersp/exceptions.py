class ReverseTimeError(Exception):
    def __init__(self, message, bubble, pre_skip, post_skip):
        super().__init__(message)
        # Display the errors
        print("="*80)
        print("="*35, "ERROR!", "="*37)
        print('Reverse time skip detected in')
        print(bubble)
        print('\nSpecifically, from', pre_skip, 'to', post_skip)
        print('Please check your log file at this location.')
        print('If this isn\'t an actual reverse time skip, please notify us.')
        print("="*80)
        print("="*80)
