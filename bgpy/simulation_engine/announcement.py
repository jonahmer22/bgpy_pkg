# import the C extension
from ._announcement_c import Announcement as _CAnnouncement

# create a class from the C extension
class Announcement(_CAnnouncement):
    """BGP Announcement"""