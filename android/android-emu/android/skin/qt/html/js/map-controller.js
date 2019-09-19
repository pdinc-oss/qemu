class MapController extends GoogleMapPageComponent {
    constructor(routeViewModel, routePanel, searchBox, locationPanel) {
        super();
        this.viewModel = routeViewModel;
        this.routePanel = routePanel;
        this.searchBox = searchBox;
        this.locationPanel = locationPanel;
        this.mapManager = null;
        this.currentLocationMarker = null;
        this.isGpxKmlPlaying = false;
    }

    onMapInitialized(mapManager, eventBus) {
        this.mapManager = mapManager;
        this.eventBus = eventBus;
        const self = this;
        eventBus.onMapClicked((event) => self.onWaypointSelected(event));
        eventBus.onSearchBoxPlaceChanged((place) => self.onWaypointSelected({ latLng: place.geometry.location }));
        eventBus.onSearchBoxCleared(() => self.onSearchBoxCleared());
        eventBus.on('floatingActionButtonClicked', () => self.onBeginBuildingRouteButtonClicked());
        eventBus.on('routePanelClosed', () => self.onRoutePanelClosed());
        eventBus.on('transportationModeChanged', (e) => self.onTransportModeChanged(e.newMode));
        eventBus.on('swapOriginWithDestination', () => self.swapOriginDestination());
        eventBus.on('waypoints_changed', () => self.onWaypointsChanged());
    }

    setCurrentLocation(latLng) {
        console.log('MapController::setCurrentLocation called', latLng.lat(), latLng.lng());
        if (this.currentLocationMarker) {
            this.currentLocationMarker.setMap(null);
        }
        this.currentLocationMarker = new google.maps.Marker({ map: this.mapManager.map, position: latLng });
        let blueDot = 'data:image/svg+xml, \
                <svg xmlns="http://www.w3.org/2000/svg" width="24px" height="24px" \
                        viewBox="0 0 24 24" fill="%23000000"> \
                    <circle cx="12" cy="12" r="10" fill="black" opacity=".1" /> \
                    <circle cx="12" cy="12" r="8" stroke="white" stroke-width="2" \
                        fill="%234286f5" /> \
                </svg>';
        let image = {
            url: blueDot,
            size: new google.maps.Size(24, 24),
            origin: new google.maps.Point(0, 0),
            anchor: new google.maps.Point(12, 12),
            scaledSize: new google.maps.Size(25, 25)
        };
        this.currentLocationMarker.setIcon(image);
        this.mapManager.centerMapOnLocation(latLng);
        $('#locationInfoOverlay').html(this.viewModel.formatLatLngString(latLng));
    }

    setRoute(routeJson) {
        console.log('MapController::setRoute called', routeJson);
        if (routeJson.length > 0) {
            this.routePanel.close();
            this.mapManager.clearDirections();
            this.mapManager.clearMarkers();
            this.viewModel.setRoute(routeJson);
            // this.viewModel.renderDirections(this.mapManager);
            this.viewModel.setEditable(false);
            this.showRouteEditor();
        }
        this.searchBox.update('');
        channel.objects.emulocationserver.onSavedRouteDrawn();
    }

    showRoutePlaybackPanel(visible) {
        console.log('MapController::showRoutePlaybackPanel called', visible);
        if (visible) {
            // in route playback mode
            this.isGpxKmlPlaying = true;
            this.locationPanel.hide();
            this.routePanel.close();
            this.searchBox.hide();
            this.mapManager.map.setOptions({ zoomControl: false });
            $('#locationInfoOverlay').css('display', 'flex');
        } else {
            if (this.isGpxKmlPlaying) {
                $('#locationInfoOverlay').css('display', 'none');
                this.showGpxKmlPanel();
            } else {
                this.locationPanel.hide();
                this.searchBox.show();
                this.routePanel.open();
                this.mapManager.map.setOptions({ zoomControl: true });
                $('#locationInfoOverlay').css('display', 'none');
            }
        }
    }

    showGpxKmlPanel(title, subtitle) {
        this.isGpxKmlPlaying = true;
        this.routePanel.close();
        this.searchBox.hide();
        this.locationPanel.showTitleSubtitle(title || '', subtitle || '');
    }

    hideGpxKmlPanel() {
        this.isGpxKmlPlaying = false;
        this.routePanel.close();
        this.locationPanel.hide();
        this.searchBox.show();
    }

    showRoute(routeJson, title, subtitle) {
        console.log('MapController::showRoute called', routeJson, title, subtitle);
        if (routeJson.length < 2) {
            return;
        }

        this.mapManager.clearDirections();
        showGpxKmlOverlay(title, subtitle);

        const theRoute = JSON.parse(routeJson);
        const path = theRoute.path;

        this.mapManager.addMarker(path[0], {
            label: { text: "A", color: "white" }
        });

        this.mapManager.addMarker(path[path.length - 1], {
            label: { text: "B", color: "white" }
        });

        this.mapManager.addPolylinePath(path, {
            geodesic: true,
            strokeColor: "#709ddf",
            strokeOpacity: 1.0,
            strokeWeight: 2
        });

        this.mapManager.zoomToPath(path);
        channel.objects.emulocationserver.onSavedRouteDrawn();
    }

    displayRouteEditor(latLng, address) {
        console.log('MapController::displayRouteEditor called', latLng, address);
        this.viewModel.clearWaypoints();
        this.onWaypointSelected({ latLng });
        this.routePanel.open();
    }

    showRouteEditor() {
        this.locationPanel.hide();
        this.routePanel.open();
    }

    async onWaypointSelected(event) {
        if (this.isGpxKmlPlaying) {
            return;
        }
        const latLng = event.latLng;
        const self = this;
        if (this.viewModel.isEditingRoute) {
            if (!this.viewModel.isValidRoute()) {
                const waypoint = await self.viewModel.geocodeLocation(latLng, self.mapManager.getBounds());
                this.viewModel.addNewWaypoint(waypoint);
            }
        }
        else {
            this.mapManager.clearMarkers();
            this.mapManager.addMarker(event.latLng);
            this.searchBox.showSpinner();
            setTimeout(async () => {
                self.searchBox.update(self.viewModel.formatLatLngString(latLng));
                const destination = await self.viewModel.geocodeLocation(latLng, self.mapManager.getBounds());
                self.searchBox.hideSpinner();
                self.searchBox.update(destination.address);
                self.locationPanel.show(destination.address, destination.latLng);
                self.viewModel.setDestination(destination);
                self.mapManager.centerMapOnLocation(latLng);
            }, 1);
        }
    }

    onWaypointsChanged() {
        this.viewModel.renderDirections(this.mapManager);
    }

    onBeginBuildingRouteButtonClicked() {
        this.showRouteEditor();
    }

    onSearchBoxCleared() {
        this.mapManager.clearMarkers();
        this.viewModel.setDestination();
        this.searchBox.update('');
        this.locationPanel.hide();
    }

    onRoutePanelClosed() {
        this.viewModel.setEditable(true);
        this.searchBox.clear();
        this.mapManager.clearMarkers();
        this.mapManager.clearDirections();
        this.viewModel.clearWaypoints();
    }

    onTransportModeChanged() {
        this.viewModel.renderDirections(this.mapManager);
    }
}
